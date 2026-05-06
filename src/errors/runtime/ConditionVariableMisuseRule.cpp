/**
 *
 *  @file ConditionVariableMisuseRule.cpp
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

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  namespace
  {
    std::string choose_message(const std::string &log)
    {
      if (icontains(log, "wait without lock"))
        return "condition variable wait without lock";

      if (icontains(log, "uninitialized condition variable") ||
          icontains(log, "invalid condition variable") ||
          icontains(log, "pthread_cond"))
      {
        return "invalid condition variable state";
      }

      return "condition variable misuse";
    }

    std::string choose_hint(const std::string &log)
    {
      if (icontains(log, "wait without lock"))
        return "call wait() with a locked std::unique_lock<std::mutex>";

      if (icontains(log, "uninitialized condition variable") ||
          icontains(log, "invalid condition variable") ||
          icontains(log, "pthread_cond"))
      {
        return "check condition variable lifetime and avoid waiting or notifying after destruction";
      }

      return "use wait(lock, predicate) and ensure the mutex is locked before waiting";
    }
  } // namespace

  class ConditionVariableMisuseRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return icontains(log, "condition variable") ||
             icontains(log, "condition_variable") ||
             icontains(log, "pthread_cond") ||
             icontains(log, "wait on uninitialized condition variable") ||
             icontains(log, "invalid condition variable") ||
             icontains(log, "wait without lock");
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const RuntimeLocation location =
          find_best_runtime_location_or_source_hint(
              log,
              sourceFile,
              {
                  "std::condition_variable",
                  "condition_variable",
                  ".wait(",
                  ".wait_for(",
                  ".wait_until(",
                  ".notify_one(",
                  ".notify_all(",
                  "pthread_cond",
              });

      const std::string message = choose_message(log);

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

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeConditionVariableMisuseRule()
  {
    return std::make_unique<ConditionVariableMisuseRule>();
  }
} // namespace vix::cli::errors::runtime
