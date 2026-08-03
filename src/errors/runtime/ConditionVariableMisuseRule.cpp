/**
 *
 *  @file ConditionVariableMisuseRule.cpp
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

    ConditionVariableIssue classify_issue(
        const std::string &log)
    {
      if (icontains(log, "wait without lock") ||
          icontains(log, "mutex not locked") ||
          icontains(log, "unique_lock does not own the mutex") ||
          icontains(log, "operation not permitted"))
      {
        return ConditionVariableIssue::WaitWithoutLock;
      }

      if (icontains(log, "notify") &&
          (icontains(log, "destruct") ||
           icontains(log, "destroy") ||
           icontains(log, "lifetime ended") ||
           icontains(log, "use-after-free")))
      {
        return ConditionVariableIssue::NotifyAfterDestruction;
      }

      if ((icontains(log, "destroy") ||
           icontains(log, "destruct")) &&
          (icontains(log, "waiting") ||
           icontains(log, "still in use") ||
           icontains(log, "busy")) &&
          (icontains(log, "condition variable") ||
           icontains(log, "condition_variable") ||
           icontains(log, "pthread_cond")))
      {
        return ConditionVariableIssue::DestroyedWhileWaiting;
      }

      if (icontains(log, "uninitialized condition variable") ||
          icontains(log, "invalid condition variable") ||
          icontains(log, "wait on uninitialized condition variable") ||
          icontains(log, "invalid argument") ||
          icontains(log, "EINVAL"))
      {
        return ConditionVariableIssue::InvalidConditionVariable;
      }

      if (icontains(log, "deadlock") ||
          icontains(log, "blocked") ||
          icontains(log, "timed out") ||
          icontains(log, "timeout") ||
          icontains(log, "hang"))
      {
        return ConditionVariableIssue::DeadlockOrBlockedWait;
      }

      return ConditionVariableIssue::GenericMisuse;
    }

    std::string choose_message(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case ConditionVariableIssue::WaitWithoutLock:
        return "condition variable wait requires a locked mutex";

      case ConditionVariableIssue::DestroyedWhileWaiting:
        return "condition variable is still in use";

      case ConditionVariableIssue::InvalidConditionVariable:
        return "condition variable is not valid";

      case ConditionVariableIssue::DeadlockOrBlockedWait:
        return "condition variable wait did not complete";

      case ConditionVariableIssue::NotifyAfterDestruction:
        return "condition variable lifetime has ended";

      case ConditionVariableIssue::GenericMisuse:
      default:
        return "condition variable operation failed";
      }
    }

    std::string choose_description(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case ConditionVariableIssue::WaitWithoutLock:
        return "The wait operation was called without a std::unique_lock that owns its mutex.";

      case ConditionVariableIssue::DestroyedWhileWaiting:
        return "The condition variable was destroyed while another thread could still be waiting on it.";

      case ConditionVariableIssue::InvalidConditionVariable:
        return "The condition variable was uninitialized, already destroyed, or otherwise unavailable.";

      case ConditionVariableIssue::DeadlockOrBlockedWait:
        return "A waiting thread did not receive the state change needed to continue.";

      case ConditionVariableIssue::NotifyAfterDestruction:
        return "A notification was attempted after the condition variable or its owner had been destroyed.";

      case ConditionVariableIssue::GenericMisuse:
      default:
        return "The condition variable was used with an invalid lock, lifetime, or shared-state setup.";
      }
    }

    std::string choose_hint(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case ConditionVariableIssue::WaitWithoutLock:
        return "pass a locked std::unique_lock<std::mutex> to wait(), wait_for(), or wait_until()";

      case ConditionVariableIssue::DestroyedWhileWaiting:
        return "stop and join every waiting thread before destroying the condition variable";

      case ConditionVariableIssue::InvalidConditionVariable:
        return "check initialization and make sure the condition variable is not used after destruction";

      case ConditionVariableIssue::DeadlockOrBlockedWait:
        return "use wait(lock, predicate) and make sure a reachable code path updates the predicate and notifies";

      case ConditionVariableIssue::NotifyAfterDestruction:
        return "stop notifier threads before the condition variable owner begins destruction";

      case ConditionVariableIssue::GenericMisuse:
      default:
        return "protect the predicate with the same mutex and use wait(lock, predicate)";
      }
    }

    std::vector<std::string>
    source_patterns_for_condition_variable()
    {
      /*
       * Search only for operations that may be responsible.
       *
       * Type declarations such as std::condition_variable are
       * intentionally excluded because they could highlight an
       * unrelated member declaration.
       */
      return {
          ".wait(",
          ".wait_for(",
          ".wait_until(",
          ".notify_one(",
          ".notify_all(",
          "pthread_cond_wait(",
          "pthread_cond_timedwait(",
          "pthread_cond_signal(",
          "pthread_cond_broadcast(",
          "pthread_cond_destroy(",
      };
    }

    bool looks_like_condition_variable_log(
        const std::string &log)
    {
      return icontains(log, "condition variable") ||
             icontains(log, "condition_variable") ||
             icontains(log, "pthread_cond") ||
             icontains(
                 log,
                 "wait on uninitialized condition variable") ||
             icontains(log, "invalid condition variable") ||
             icontains(log, "wait without lock");
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

  class ConditionVariableMisuseRule final
      : public IRuntimeErrorRule
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
      const std::string message =
          choose_message(log);

      const std::string description =
          choose_description(log);

      const std::string hint =
          choose_hint(log);

      RuntimeLocation location =
          find_best_runtime_location(
              log,
              sourceFile);

      if (!location.valid())
      {
        location =
            find_best_runtime_location_or_source_hint(
                log,
                sourceFile,
                source_patterns_for_condition_variable());
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
  makeConditionVariableMisuseRule()
  {
    return std::make_unique<
        ConditionVariableMisuseRule>();
  }
} // namespace vix::cli::errors::runtime
