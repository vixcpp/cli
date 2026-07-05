/**
 *
 *  @file TimeoutRuntimeRule.cpp
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
#include <string>
#include <vector>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  namespace
  {
    std::string choose_message(const std::string &log)
    {
      (void)log;
      return "operation timed out";
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "increase timeout, check network/service availability, or make the operation cancellable";
    }

    std::vector<std::string> source_patterns_for_timeout()
    {
      return {
          "wait_for",
          "wait_until",
          "sleep_for",
          "timeout",
          "connect",
          "read",
          "write",
      };
    }

    bool looks_like_timeout_log(const std::string &log)
    {
      if (icontains(log, "ETIMEDOUT") ||
          icontains(log, "operation timed out") ||
          icontains(log, "deadline exceeded"))
      {
        return true;
      }

      // Require both "timed out" or "timeout" with a stronger signal
      // to avoid false positives on logs that merely mention timeouts.
      if (icontains(log, "timed out"))
        return true;

      return false;
    }
  } // namespace

  class TimeoutRuntimeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_timeout_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const std::string message = choose_message(log);

      RuntimeLocation location = find_best_runtime_location(log, sourceFile);

      if (!location.valid())
      {
        location = find_best_runtime_location_or_source_hint(
            log,
            sourceFile,
            source_patterns_for_timeout());
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
              "the runtime log below shows which operation hit the timeout",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeTimeoutRuntimeRule()
  {
    return std::make_unique<TimeoutRuntimeRule>();
  }
} // namespace vix::cli::errors::runtime
