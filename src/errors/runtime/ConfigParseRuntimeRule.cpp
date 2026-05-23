/**
 *
 *  @file ConfigParseRuntimeRule.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/errors/runtime/IRuntimeErrorRule.hpp>
#include <vix/cli/errors/runtime/RuntimeRuleUtils.hpp>

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
      EnvParseError,
      InvalidEnvLine,
      InvalidKey,
      GenericConfigParse,
    };

    ConfigParseKind classify_issue(const std::string &log)
    {
      if (icontains(log, "failed to parse .env content at line"))
        return ConfigParseKind::EnvParseError;

      if (icontains(log, "invalid .env line"))
        return ConfigParseKind::InvalidEnvLine;

      if (icontains(log, "key is invalid"))
        return ConfigParseKind::InvalidKey;

      return ConfigParseKind::GenericConfigParse;
    }

    std::optional<RuntimeLocation> try_extract_env_location(
        const std::string &log,
        const std::filesystem::path &sourceFile)
    {
      static const std::regex re(
          R"(failed to parse[ \t]+([^ \t\r\n]+)[ \t]+content[ \t]+at[ \t]+line[ \t]+([0-9]+))",
          std::regex::icase);

      std::smatch match;
      if (!std::regex_search(log, match, re))
        return std::nullopt;

      if (match.size() < 3)
        return std::nullopt;

      const std::filesystem::path reportedPath = match[1].str();

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
        if (!sourceFile.empty() && sourceFile.has_parent_path())
          candidates.push_back(sourceFile.parent_path() / reportedPath);

        candidates.push_back(std::filesystem::current_path() / reportedPath);
        candidates.push_back(reportedPath);
      }

      RuntimeLocation location{};
      location.line = line;
      location.column = 1;

      for (const auto &candidate : candidates)
      {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec))
        {
          location.file = candidate;
          return location;
        }
      }

      if (!candidates.empty())
      {
        location.file = candidates.front();
        return location;
      }

      return std::nullopt;
    }

    std::string choose_message(const std::string &log)
    {
      (void)log;
      return "environment configuration parse failed";
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "fix the invalid environment file line; use KEY=value with a valid key name";
    }

    std::vector<std::string> source_patterns_for_config()
    {
      return {
          "vix::config::Config",
          "Config config",
          "Config{",
          ".env",
      };
    }

    bool looks_like_config_parse_log(const std::string &log)
    {
      return icontains(log, "Failed to load environment configuration") ||
             icontains(log, "failed to parse .env content at line") ||
             icontains(log, "invalid .env line") ||
             icontains(log, "key is invalid") ||
             icontains(log, "configuration parse") ||
             icontains(log, "config parse");
    }
  } // namespace

  class ConfigParseRuntimeRule final : public IRuntimeErrorRule
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
      const std::string message = choose_message(log);

      RuntimeLocation location{};

      if (const auto envLocation = try_extract_env_location(log, sourceFile))
      {
        location = *envLocation;
      }
      else
      {
        location = find_best_runtime_location_or_source_hint(
            log,
            sourceFile,
            source_patterns_for_config());
      }

      std::cerr << RED
                << "runtime error: "
                << message
                << RESET << "\n";

      if (location.valid())
      {
        const auto err = make_runtime_location(
            location.file,
            location.line,
            location.column,
            message);

        print_runtime_codeframe(err);
      }

      print_runtime_hints_and_at(
          {
              choose_hint(log),
              "keys must match [A-Za-z_][A-Za-z0-9_]* and lines must contain a single '=' separator",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeConfigParseRuntimeRule()
  {
    return std::make_unique<ConfigParseRuntimeRule>();
  }
} // namespace vix::cli::errors::runtime
