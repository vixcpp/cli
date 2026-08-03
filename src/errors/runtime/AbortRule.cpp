/**
 *
 *  @file AbortRule.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the LICENSE file.
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
#include <vector>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  namespace
  {
    enum class AbortKind
    {
      EnvConfigParseError,
      ThreadJoinable,
      TerminateWithoutActiveException,
      UncaughtException,
      ExplicitAbort,
      Assertion,
      Sigabrt,
      GenericTerminate,
      GenericAbort,
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

    std::optional<RuntimeLocation> try_extract_env_config_location(
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

      const std::filesystem::path reportedPath =
          match[1].str();

      const int line =
          std::stoi(match[2].str());

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

        candidates.push_back(
            std::filesystem::current_path() /
            reportedPath);

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
                error))
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

    bool is_env_config_parse_error(
        const std::string &log)
    {
      return icontains(
                 log,
                 "Failed to load environment configuration") &&
             icontains(log, "failed to parse") &&
             icontains(log, "content at line");
    }

    AbortKind classify_abort(
        const std::string &log)
    {
      if (is_env_config_parse_error(log))
        return AbortKind::EnvConfigParseError;

      if (icontains(
              log,
              "terminate called without an active exception") ||
          icontains(
              log,
              "terminate called without active exception"))
      {
        return AbortKind::TerminateWithoutActiveException;
      }

      if (icontains(log, "std::thread") ||
          icontains(log, "thread::~thread") ||
          icontains(log, "~thread") ||
          icontains(log, "joinable"))
      {
        return AbortKind::ThreadJoinable;
      }

      if (icontains(
              log,
              "terminate called after throwing") ||
          icontains(log, "what():"))
      {
        return AbortKind::UncaughtException;
      }

      if (icontains(log, "assert") ||
          icontains(log, "assertion"))
      {
        return AbortKind::Assertion;
      }

      if (icontains(log, "abort()") ||
          icontains(log, "std::abort"))
      {
        return AbortKind::ExplicitAbort;
      }

      if (icontains(log, "sigabrt") ||
          icontains(log, "signal 6"))
      {
        return AbortKind::Sigabrt;
      }

      if (icontains(log, "terminate called") ||
          icontains(log, "std::terminate"))
      {
        return AbortKind::GenericTerminate;
      }

      return AbortKind::GenericAbort;
    }

    std::string choose_message(
        const std::string &log)
    {
      switch (classify_abort(log))
      {
      case AbortKind::EnvConfigParseError:
        return "environment configuration is invalid";

      case AbortKind::ThreadJoinable:
        return "thread was not finished";

      case AbortKind::TerminateWithoutActiveException:
        return "application stopped unexpectedly";

      case AbortKind::UncaughtException:
        return "operation cannot continue";

      case AbortKind::ExplicitAbort:
        return "application stopped";

      case AbortKind::Assertion:
        return "assertion failed";

      case AbortKind::Sigabrt:
        return "application was forced to stop";

      case AbortKind::GenericTerminate:
        return "application stopped unexpectedly";

      case AbortKind::GenericAbort:
      default:
        return "application stopped";
      }
    }

    std::string choose_description(
        const std::string &log)
    {
      switch (classify_abort(log))
      {
      case AbortKind::EnvConfigParseError:
        return "The environment file contains a value that could not be read.";

      case AbortKind::ThreadJoinable:
        return "A running thread was destroyed before it was finished.";

      case AbortKind::TerminateWithoutActiveException:
        return "The program reached a state it could not safely continue from.";

      case AbortKind::UncaughtException:
        return {};

      case AbortKind::ExplicitAbort:
        return "The program requested an immediate stop.";

      case AbortKind::Assertion:
        return "A condition expected by the program was false.";

      case AbortKind::Sigabrt:
        return "The program received an immediate stop signal.";

      case AbortKind::GenericTerminate:
        return "The program reached a state it could not safely continue from.";

      case AbortKind::GenericAbort:
      default:
        return "The program stopped before it could finish.";
      }
    }

    std::string choose_hint(
        const std::string &log,
        bool hasLocation)
    {
      switch (classify_abort(log))
      {
      case AbortKind::EnvConfigParseError:
        return "use KEY=value and check the highlighted configuration line";

      case AbortKind::ThreadJoinable:
        return "finish the thread with join() or detach it before it is destroyed";

      case AbortKind::TerminateWithoutActiveException:
        return "make sure running threads are finished before their owner is destroyed";

      case AbortKind::UncaughtException:
        return {};

      case AbortKind::ExplicitAbort:
        return "remove the abort call or make sure this immediate stop is intentional";

      case AbortKind::Assertion:
        return "check why the highlighted condition became false";

      case AbortKind::Sigabrt:
        if (hasLocation)
          return "check the highlighted operation that stopped the program";

        return "run with VIX_LOG_LEVEL=debug to inspect the original runtime output";

      case AbortKind::GenericTerminate:
        if (hasLocation)
          return "check the highlighted operation that stopped the program";

        return "run with VIX_LOG_LEVEL=debug to inspect the original runtime output";

      case AbortKind::GenericAbort:
      default:
        if (hasLocation)
          return "check the highlighted line and make sure this immediate stop is intentional";

        return "run with VIX_LOG_LEVEL=debug to inspect the original runtime output";
      }
    }

    std::vector<std::string> source_patterns_for_abort(
        const std::string &log)
    {
      std::vector<std::string> patterns = {
          "std::abort",
          "abort()",
          "abort(",
          "assert(",
          "throw ",
          "std::terminate",
          "terminate()",
      };

      if (is_env_config_parse_error(log))
      {
        patterns.push_back("vix::config::Config");
        patterns.push_back("Config ");
        patterns.push_back("Config{");
        patterns.push_back("Config config");
      }

      const bool looksThreadRelated =
          icontains(log, "std::thread") ||
          icontains(log, "thread::~thread") ||
          icontains(log, "~thread") ||
          icontains(log, "joinable") ||
          icontains(
              log,
              "terminate called without an active exception") ||
          icontains(
              log,
              "terminate called without active exception");

      if (looksThreadRelated)
      {
        patterns.push_back(".start()");
        patterns.push_back(".listen_blocking()");
        patterns.push_back(".stop()");
        patterns.push_back(".join()");
        patterns.push_back("std::thread");
        patterns.push_back("RuntimeExecutor");
      }

      return patterns;
    }

    bool looks_like_abort_log(
        const std::string &log)
    {
      return icontains(log, "aborted") ||
             icontains(log, "sigabrt") ||
             icontains(log, "signal 6") ||
             icontains(log, "abort()") ||
             icontains(log, "std::abort") ||
             icontains(log, "core dumped") ||
             icontains(log, "terminate called") ||
             icontains(log, "std::terminate") ||
             icontains(
                 log,
                 "terminate called without an active exception") ||
             icontains(
                 log,
                 "terminate called without active exception");
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

      /*
       * Do not pass sourceFile as a fallback here.
       *
       * "source: main.cpp" is not a real error location.
       * The location is printed only when Vix found an exact
       * line or a useful source hint.
       */
      const std::string at =
          location.valid()
              ? make_at_text(
                    location,
                    std::filesystem::path{})
              : std::string{};

      print_runtime_hints_and_at(
          hints,
          at);
    }

    void print_debug_log(
        const std::string &log)
    {
      if (!technical_details_enabled())
        return;

      print_runtime_log_excerpt(log);
    }
  } // namespace

  class AbortRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return looks_like_abort_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const std::string message =
          choose_message(log);

      const std::string description =
          choose_description(log);

      RuntimeLocation location{};

      if (const auto envLocation =
              try_extract_env_config_location(
                  log,
                  sourceFile))
      {
        location = *envLocation;
      }
      else
      {
        /*
         * Keep the existing runtime-location contract.
         *
         * First use a real location reported by the runtime.
         * If none exists, search the source for a relevant
         * operation such as std::abort() or assert().
         */
        location =
            find_best_runtime_location_or_source_hint(
                log,
                sourceFile,
                source_patterns_for_abort(log));
      }

      const std::string hint =
          choose_hint(
              log,
              location.valid());

      print_error(
          message,
          description);

      if (location.valid())
      {
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

      /*
       * Native C++ output is hidden by default.
       * It remains available through:
       *
       * VIX_LOG_LEVEL=debug vix run ...
       */
      print_debug_log(log);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule>
  makeAbortRule()
  {
    return std::make_unique<AbortRule>();
  }
} // namespace vix::cli::errors::runtime
