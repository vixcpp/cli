/**
 *
 *  @file AbortRule.cpp
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

      const std::filesystem::path reportedPath = match[1].str();
      const int line = std::stoi(match[2].str());

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

    bool is_env_config_parse_error(const std::string &log)
    {
      return icontains(log, "Failed to load environment configuration") &&
             icontains(log, "failed to parse") &&
             icontains(log, "content at line");
    }

    AbortKind classify_abort(const std::string &log)
    {
      if (is_env_config_parse_error(log))
        return AbortKind::EnvConfigParseError;

      if (icontains(log, "terminate called without an active exception") ||
          icontains(log, "terminate called without active exception"))
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

      if (icontains(log, "terminate called after throwing") ||
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

    std::string choose_message(const std::string &log)
    {
      switch (classify_abort(log))
      {
      case AbortKind::EnvConfigParseError:
        return "environment configuration parse failed";

      case AbortKind::ThreadJoinable:
        return "joinable std::thread destroyed";

      case AbortKind::TerminateWithoutActiveException:
        return "std::terminate called without active exception";

      case AbortKind::UncaughtException:
        return "uncaught exception reached std::terminate";

      case AbortKind::ExplicitAbort:
        return "std::abort called";

      case AbortKind::Assertion:
        return "assertion aborted the program";

      case AbortKind::Sigabrt:
        return "SIGABRT received";

      case AbortKind::GenericTerminate:
        return "std::terminate called";

      case AbortKind::GenericAbort:
      default:
        return "aborted";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_abort(log))
      {
      case AbortKind::EnvConfigParseError:
        return "fix the invalid environment file line; use KEY=value with a valid key name";

      case AbortKind::ThreadJoinable:
        return "a std::thread was still joinable during destruction; call join(), detach(), or stop the owner before returning";

      case AbortKind::TerminateWithoutActiveException:
        return "this usually means a joinable std::thread was destroyed; check server shutdown, worker threads, and RAII destructors";

      case AbortKind::UncaughtException:
        return "catch the exception at the thread/task boundary and log e.what() before shutdown";

      case AbortKind::ExplicitAbort:
        return "check the code path that explicitly calls abort() or std::abort()";

      case AbortKind::Assertion:
        return "check the failed assertion and the condition that was expected to be true";

      case AbortKind::Sigabrt:
        return "the process received SIGABRT; inspect the runtime log below for the original abort reason";

      case AbortKind::GenericTerminate:
        return "check uncaught exceptions, joinable std::thread destruction, or noexcept violations";

      case AbortKind::GenericAbort:
      default:
        return "check assertions, std::terminate(), uncaught exceptions, and invalid runtime states";
      }
    }

    std::vector<std::string> source_patterns_for_abort(const std::string &log)
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
          icontains(log, "terminate called without an active exception") ||
          icontains(log, "terminate called without active exception");

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

    bool looks_like_abort_log(const std::string &log)
    {
      return icontains(log, "aborted") ||
             icontains(log, "sigabrt") ||
             icontains(log, "signal 6") ||
             icontains(log, "abort()") ||
             icontains(log, "std::abort") ||
             icontains(log, "core dumped") ||
             icontains(log, "terminate called") ||
             icontains(log, "std::terminate") ||
             icontains(log, "terminate called without an active exception") ||
             icontains(log, "terminate called without active exception");
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
      const std::string message = choose_message(log);

      RuntimeLocation location{};

      if (const auto envLocation =
              try_extract_env_config_location(log, sourceFile))
      {
        location = *envLocation;
      }
      else
      {
        location =
            find_best_runtime_location_or_source_hint(
                log,
                sourceFile,
                source_patterns_for_abort(log));
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
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeAbortRule()
  {
    return std::make_unique<AbortRule>();
  }
} // namespace vix::cli::errors::runtime
