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
#include <vector>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
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
      std::string title = "runtime error: condition variable misuse";
      std::vector<std::string> hints = {
          "a condition variable was likely used incorrectly",
          "check waits without a locked std::unique_lock, invalid lifetime, and missing predicate-based waits",
      };

      if (icontains(log, "wait without lock"))
      {
        title = "runtime error: condition variable wait without lock";
        hints = {
            "wait() must be called with a locked std::unique_lock<std::mutex>",
            "lock the mutex before waiting and use wait(lock, predicate) when possible",
        };
      }
      else if (icontains(log, "uninitialized condition variable") ||
               icontains(log, "invalid condition variable") ||
               icontains(log, "pthread_cond"))
      {
        title = "runtime error: invalid condition variable state";
        hints = {
            "the condition variable may be uninitialized, destroyed, or otherwise invalid",
            "check object lifetime and avoid waiting or notifying after destruction",
        };
      }

      std::cerr << RED
                << title
                << RESET << "\n";

      print_runtime_hints_and_at(
          hints,
          !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeConditionVariableMisuseRule()
  {
    return std::make_unique<ConditionVariableMisuseRule>();
  }
} // namespace vix::cli::errors::runtime
