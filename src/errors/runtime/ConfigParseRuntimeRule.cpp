/**
 *
 *  @file ConfigParseRuntimeRule.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/errors/runtime/IRuntimeErrorRule.hpp>
#include <vix/cli/errors/runtime/RuntimeRuleUtils.hpp>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <system_error>
#include <vector>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  namespace
  {
    enum class ConfigParseKind
    {
      EnvironmentFile,
      GenericConfiguration,
    };

    std::string trim_copy(const std::string &value)
    {
      std::size_t begin = 0;

      while (begin < value.size() &&
             std::isspace(
                 static_cast<unsigned char>(value[begin])) != 0)
      {
        ++begin;
      }

      std::size_t end = value.size();

      while (end > begin &&
             std::isspace(
                 static_cast<unsigned char>(value[end - 1])) != 0)
      {
        --end;
      }

      return value.substr(begin, end - begin);
    }

    std::string lowercase_copy(std::string value)
    {
      for (char &character : value)
      {
        character = static_cast<char>(
            std::tolower(
                static_cast<unsigned char>(character)));
      }

      return value;
    }

    bool technical_details_enabled()
    {
      const char *level = std::getenv("VIX_LOG_LEVEL");

      if (level == nullptr || *level == '\0')
        return false;

      const std::string value =
          lowercase_copy(trim_copy(level));

      return value == "debug" ||
             value == "trace";
    }

    bool is_environment_config_log(
        const std::string &log)
    {
      return icontains(
                 log,
                 "environment configuration") ||
             icontains(log, ".env") ||
             icontains(log, "dotenv");
    }

    ConfigParseKind classify_issue(
        const std::string &log)
    {
      if (is_environment_config_log(log))
        return ConfigParseKind::EnvironmentFile;

      return ConfigParseKind::GenericConfiguration;
    }

    std::optional<RuntimeLocation>
    try_extract_env_location(
        const std::string &log,
        const std::filesystem::path &sourceFile)
    {
      static const std::regex re(
          R"(failed to parse[ \t]+["']?([^"' \t\r\n]+)["']?[ \t]+content[ \t]+at[ \t]+line[ \t]+([0-9]+))",
          std::regex::icase);

      std::smatch match;

      if (!std::regex_search(log, match, re))
        return std::nullopt;

      if (match.size() < 3)
        return std::nullopt;

      const std::filesystem::path reportedPath =
          match[1].str();

      int line = 0;

      try
      {
        line = std::stoi(match[2].str());
      }
      catch (...)
      {
        return std::nullopt;
      }

      if (line <= 0)
        return std::nullopt;

      std::vector<std::filesystem::path> candidates;

      if (reportedPath.is_absolute())
      {
        candidates.push_back(reportedPath);
      }
      else
      {
        if (!sourceFile.empty() &&
            sourceFile.has_parent_path())
        {
          candidates.push_back(
              sourceFile.parent_path() /
              reportedPath);
        }

        std::error_code currentPathError;

        const std::filesystem::path currentPath =
            std::filesystem::current_path(
                currentPathError);

        if (!currentPathError)
        {
          candidates.push_back(
              currentPath /
              reportedPath);
        }

        candidates.push_back(reportedPath);
      }

      RuntimeLocation location{};
      location.line = line;
      location.column = 1;

      for (const auto &candidate : candidates)
      {
        std::error_code error;

        if (std::filesystem::exists(
                candidate,
                error) &&
            !error)
        {
          location.file = candidate;
          return location;
        }
      }

      /*
       * Preserve the path reported by the runtime even if it
       * cannot currently be opened. CodeFrame and make_at_text()
       * will still reject unusable files safely.
       */
      if (!candidates.empty())
      {
        location.file = candidates.front();
        return location;
      }

      return std::nullopt;
    }

    std::string choose_message(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case ConfigParseKind::EnvironmentFile:
        return "environment configuration is invalid";

      case ConfigParseKind::GenericConfiguration:
      default:
        return "configuration could not be parsed";
      }
    }

    std::string choose_description(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case ConfigParseKind::EnvironmentFile:
        return "The environment file contains an entry that does not follow the expected format.";

      case ConfigParseKind::GenericConfiguration:
      default:
        return "The configuration contains invalid syntax or an unsupported value.";
      }
    }

    std::string choose_hint(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case ConfigParseKind::EnvironmentFile:
        return "use KEY=value, with a key matching [A-Za-z_][A-Za-z0-9_]*";

      case ConfigParseKind::GenericConfiguration:
      default:
        return "check the configuration syntax, required fields, and value types";
      }
    }

    std::vector<std::string>
    source_patterns_for_config(
        const std::string &log)
    {
      if (classify_issue(log) ==
          ConfigParseKind::EnvironmentFile)
      {
        return {
            "vix::config::Config",
            "Config config",
            "Config{",
            ".env",
        };
      }

      return {
          "Config config",
          "Config{",
      };
    }

    bool looks_like_config_parse_log(
        const std::string &log)
    {
      if (icontains(
              log,
              "Failed to load environment configuration") ||
          icontains(
              log,
              "failed to parse .env content at line") ||
          icontains(log, "invalid .env line"))
      {
        return true;
      }

      if (icontains(log, "configuration parse") ||
          icontains(log, "config parse"))
      {
        return true;
      }

      if (icontains(log, "key is invalid") &&
          (icontains(log, ".env") ||
           icontains(log, "environment") ||
           icontains(log, "configuration")))
      {
        return true;
      }

      return false;
    }

    void print_error(
        const std::string &message,
        const std::string &description)
    {
      std::cerr << RED
                << "runtime error: "
                << message
                << RESET
                << "\n";

      if (!description.empty())
      {
        std::cerr << "  "
                  << description
                  << "\n";
      }
    }

    void print_hint_and_location(
        const std::string &hint,
        const RuntimeLocation &location)
    {
      std::vector<std::string> hints;

      if (!hint.empty())
        hints.push_back(hint);

      const std::string at =
          location.valid()
              ? make_at_text(
                    location,
                    std::filesystem::path{})
              : std::string{};

      if (hints.empty() && at.empty())
        return;

      std::cerr << "\n";

      print_runtime_hints_and_at(
          hints,
          at);
    }

    void print_debug_log(
        const std::string &log)
    {
      if (!technical_details_enabled())
        return;

      print_runtime_log_excerpt(
          log,
          20);
    }
  } // namespace

  class ConfigParseRuntimeRule final
      : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return looks_like_config_parse_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const std::string message =
          choose_message(log);

      const std::string description =
          choose_description(log);

      const std::string hint =
          choose_hint(log);

      RuntimeLocation location{};

      if (const auto envLocation =
              try_extract_env_location(
                  log,
                  sourceFile))
      {
        location = *envLocation;
      }

      if (!location.valid())
      {
        location =
            find_best_runtime_location(
                log,
                sourceFile);
      }

      if (!location.valid())
      {
        location =
            find_best_runtime_location_or_source_hint(
                log,
                sourceFile,
                source_patterns_for_config(log));
      }

      print_error(
          message,
          description);

      if (location.valid())
      {
        std::cerr << "\n";

        const auto error =
            make_runtime_location(
                location.file,
                location.line,
                location.column,
                message);

        print_runtime_codeframe(error);
      }

      print_hint_and_location(
          hint,
          location);

      print_debug_log(log);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule>
  makeConfigParseRuntimeRule()
  {
    return std::make_unique<
        ConfigParseRuntimeRule>();
  }
} // namespace vix::cli::errors::runtime
