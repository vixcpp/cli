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
  namespace
  {
    enum class ConditionVariableIssue
    {
      WaitWithoutLock,
      DestroyedWhileWaiting,
      InvalidConditionVariable,
      DeadlockOrBlockedWait,
      NotifyAfterDestruction,
      GenericMisuse,
    };

    ConditionVariableIssue classify_issue(const std::string &log)
    {
      if (icontains(log, "wait without lock") ||
          icontains(log, "mutex not locked") ||
          icontains(log, "operation not permitted"))
      {
        return ConditionVariableIssue::WaitWithoutLock;
      }

      if (icontains(log, "destroy") &&
          (icontains(log, "condition variable") ||
           icontains(log, "condition_variable") ||
           icontains(log, "pthread_cond")))
      {
        return ConditionVariableIssue::DestroyedWhileWaiting;
      }

      if (icontains(log, "notify") &&
          (icontains(log, "destruct") ||
           icontains(log, "destroy") ||
           icontains(log, "lifetime")))
      {
        return ConditionVariableIssue::NotifyAfterDestruction;
      }

      if (icontains(log, "uninitialized condition variable") ||
          icontains(log, "invalid condition variable") ||
          icontains(log, "pthread_cond_wait") ||
          icontains(log, "pthread_cond_timedwait") ||
          icontains(log, "pthread_cond_signal") ||
          icontains(log, "pthread_cond_broadcast"))
      {
        return ConditionVariableIssue::InvalidConditionVariable;
      }

      if (icontains(log, "deadlock") ||
          icontains(log, "blocked") ||
          icontains(log, "timeout") ||
          icontains(log, "hang"))
      {
        return ConditionVariableIssue::DeadlockOrBlockedWait;
      }

      return ConditionVariableIssue::GenericMisuse;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case ConditionVariableIssue::WaitWithoutLock:
        return "condition variable wait without locked mutex";

      case ConditionVariableIssue::DestroyedWhileWaiting:
        return "condition variable destroyed while still in use";

      case ConditionVariableIssue::InvalidConditionVariable:
        return "invalid condition variable state";

      case ConditionVariableIssue::DeadlockOrBlockedWait:
        return "condition variable wait may be blocked";

      case ConditionVariableIssue::NotifyAfterDestruction:
        return "condition variable notified after lifetime ended";

      case ConditionVariableIssue::GenericMisuse:
      default:
        return "condition variable misuse";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case ConditionVariableIssue::WaitWithoutLock:
        return "call wait(), wait_for(), or wait_until() with a locked std::unique_lock<std::mutex>";

      case ConditionVariableIssue::DestroyedWhileWaiting:
        return "ensure all waiting threads are stopped and joined before destroying the condition variable";

      case ConditionVariableIssue::InvalidConditionVariable:
        return "check condition variable lifetime, initialization, and avoid using it after destruction";

      case ConditionVariableIssue::DeadlockOrBlockedWait:
        return "use wait(lock, predicate) and verify that notify_one() or notify_all() is always reachable";

      case ConditionVariableIssue::NotifyAfterDestruction:
        return "do not notify a condition variable after the owner object has started destruction";

      case ConditionVariableIssue::GenericMisuse:
      default:
        return "use wait(lock, predicate), protect shared state with the same mutex, and notify after updating the predicate";
      }
    }

    std::vector<std::string> source_patterns_for_condition_variable()
    {
      return {
          "std::condition_variable",
          "std::condition_variable_any",
          "condition_variable",
          ".wait(",
          ".wait_for(",
          ".wait_until(",
          ".notify_one(",
          ".notify_all(",
          "pthread_cond_wait",
          "pthread_cond_timedwait",
          "pthread_cond_signal",
          "pthread_cond_broadcast",
          "pthread_cond_destroy",
      };
    }

    bool looks_like_condition_variable_log(const std::string &log)
    {
      return icontains(log, "condition variable") ||
             icontains(log, "condition_variable") ||
             icontains(log, "pthread_cond") ||
             icontains(log, "wait on uninitialized condition variable") ||
             icontains(log, "invalid condition variable") ||
             icontains(log, "wait without lock");
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
      return looks_like_condition_variable_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const std::string message = choose_message(log);

      const RuntimeLocation location =
          find_best_runtime_location_or_source_hint(
              log,
              sourceFile,
              source_patterns_for_condition_variable());

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

  std::unique_ptr<IRuntimeErrorRule> makeConditionVariableMisuseRule()
  {
    return std::make_unique<ConditionVariableMisuseRule>();
  }
} // namespace vix::cli::errors::runtime
