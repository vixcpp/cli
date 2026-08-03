/**
 *
 *  @file DeadlockRule.cpp
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
    enum class DeadlockKind
    {
      LockOrderInversion,
      ResourceDeadlockAvoided,
      SelfDeadlock,
      RecursiveLock,
      MutexCycle,
      ThreadSanitizerDeadlock,
      GenericDeadlock,
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

    DeadlockKind classify_deadlock(
        const std::string &log)
    {
      if (icontains(log, "lock-order-inversion") ||
          icontains(log, "lock order inversion") ||
          icontains(log, "cycle in lock order graph"))
      {
        return DeadlockKind::LockOrderInversion;
      }

      if (icontains(log, "resource deadlock avoided") ||
          icontains(log, "EDEADLK") ||
          icontains(log, "E DEADLK"))
      {
        return DeadlockKind::ResourceDeadlockAvoided;
      }

      if (icontains(log, "self-deadlock") ||
          icontains(log, "self deadlock") ||
          icontains(log, "would deadlock") ||
          icontains(log, "already owns the mutex") ||
          icontains(log, "mutex already locked by current thread"))
      {
        return DeadlockKind::SelfDeadlock;
      }

      if (icontains(log, "recursive lock attempt") ||
          icontains(log, "recursive locking") ||
          icontains(log, "recursive mutex lock"))
      {
        return DeadlockKind::RecursiveLock;
      }

      if (icontains(log, "ThreadSanitizer") &&
          icontains(log, "deadlock"))
      {
        return DeadlockKind::ThreadSanitizerDeadlock;
      }

      if ((icontains(log, "mutex") ||
           icontains(log, "lock")) &&
          icontains(log, "cycle"))
      {
        return DeadlockKind::MutexCycle;
      }

      return DeadlockKind::GenericDeadlock;
    }

    std::string choose_message(
        const std::string &log)
    {
      switch (classify_deadlock(log))
      {
      case DeadlockKind::LockOrderInversion:
        return "mutexes were locked in conflicting orders";

      case DeadlockKind::ResourceDeadlockAvoided:
        return "locking this resource would cause a deadlock";

      case DeadlockKind::SelfDeadlock:
        return "thread tried to lock the same mutex twice";

      case DeadlockKind::RecursiveLock:
        return "mutex was locked recursively";

      case DeadlockKind::MutexCycle:
        return "mutex dependency cycle detected";

      case DeadlockKind::ThreadSanitizerDeadlock:
        return "ThreadSanitizer detected a deadlock";

      case DeadlockKind::GenericDeadlock:
      default:
        return "deadlock detected";
      }
    }

    std::string choose_description(
        const std::string &log)
    {
      switch (classify_deadlock(log))
      {
      case DeadlockKind::LockOrderInversion:
        return "Different threads acquired the same mutexes in a different order.";

      case DeadlockKind::ResourceDeadlockAvoided:
        return "The operation was stopped because the current locking sequence could not complete safely.";

      case DeadlockKind::SelfDeadlock:
        return "A non-recursive mutex was locked again by the thread that already owns it.";

      case DeadlockKind::RecursiveLock:
        return "The current code path entered another lock scope while still owning the same mutex.";

      case DeadlockKind::MutexCycle:
        return "Two or more lock dependencies form a cycle, so no waiting thread can continue.";

      case DeadlockKind::ThreadSanitizerDeadlock:
        return "ThreadSanitizer found a lock sequence that can block the involved threads permanently.";

      case DeadlockKind::GenericDeadlock:
      default:
        return "One or more threads are waiting for resources that cannot become available.";
      }
    }

    std::string choose_hint(
        const std::string &log)
    {
      switch (classify_deadlock(log))
      {
      case DeadlockKind::LockOrderInversion:
        return "always acquire the mutexes in the same order, or lock them together with std::scoped_lock";

      case DeadlockKind::ResourceDeadlockAvoided:
        return "check nested lock scopes and repeated lock() calls on the same mutex";

      case DeadlockKind::SelfDeadlock:
        return "remove the second lock or release the first lock before entering this code path";

      case DeadlockKind::RecursiveLock:
        return "prefer one lock scope; use std::recursive_mutex only when recursive ownership is intentional";

      case DeadlockKind::MutexCycle:
        return "break the cycle by defining one global order for acquiring shared resources";

      case DeadlockKind::ThreadSanitizerDeadlock:
        return "compare the lock acquisition traces and make every thread follow the same lock order";

      case DeadlockKind::GenericDeadlock:
      default:
        return "use a consistent lock order and prefer std::scoped_lock when acquiring multiple mutexes";
      }
    }

    std::string choose_secondary_hint(
        const std::string &log)
    {
      switch (classify_deadlock(log))
      {
      case DeadlockKind::LockOrderInversion:
      case DeadlockKind::MutexCycle:
      case DeadlockKind::ThreadSanitizerDeadlock:
        return "run with VIX_LOG_LEVEL=debug to inspect the complete lock traces";

      case DeadlockKind::ResourceDeadlockAvoided:
      case DeadlockKind::SelfDeadlock:
      case DeadlockKind::RecursiveLock:
      case DeadlockKind::GenericDeadlock:
      default:
        return {};
      }
    }

    bool may_use_source_hint(
        DeadlockKind kind)
    {
      /*
       * A local source search is useful only when the error is
       * probably caused by one repeated lock operation.
       *
       * Lock-order inversions and mutex cycles involve multiple
       * locations. Highlighting the first lock() in the file would
       * be misleading.
       */
      switch (kind)
      {
      case DeadlockKind::ResourceDeadlockAvoided:
      case DeadlockKind::SelfDeadlock:
      case DeadlockKind::RecursiveLock:
        return true;

      case DeadlockKind::LockOrderInversion:
      case DeadlockKind::MutexCycle:
      case DeadlockKind::ThreadSanitizerDeadlock:
      case DeadlockKind::GenericDeadlock:
      default:
        return false;
      }
    }

    std::vector<std::string>
    source_patterns_for_local_deadlock()
    {
      /*
       * Keep only lock operations.
       *
       * Type declarations such as std::mutex, std::lock_guard and
       * std::unique_lock are excluded because they can highlight
       * unrelated declarations instead of the failing operation.
       */
      return {
          "std::lock(",
          ".lock(",
          ".try_lock(",
          "pthread_mutex_lock(",
          "pthread_mutex_trylock(",
      };
    }

    bool looks_like_deadlock_log(
        const std::string &log)
    {
      return icontains(log, "deadlock") ||
             icontains(log, "resource deadlock avoided") ||
             icontains(log, "EDEADLK") ||
             icontains(log, "E DEADLK") ||
             icontains(log, "lock-order-inversion") ||
             icontains(log, "lock order inversion") ||
             icontains(log, "cycle in lock order graph") ||
             icontains(log, "would deadlock");
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

    void print_hints_and_location(
        const std::string &hint,
        const std::string &secondaryHint,
        const RuntimeLocation &location)
    {
      std::vector<std::string> hints;

      if (!hint.empty())
        hints.push_back(hint);

      if (!secondaryHint.empty())
        hints.push_back(secondaryHint);

      /*
       * Do not fall back to "source: main.cpp".
       *
       * A filename without a precise line is not a deadlock
       * location.
       */
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

      /*
       * Deadlock reports may contain multiple lock stacks and a
       * lock dependency graph. Keep them available in debug mode.
       */
      print_runtime_log_excerpt(
          log,
          24);
    }
  } // namespace

  class DeadlockRule final
      : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return looks_like_deadlock_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const DeadlockKind kind =
          classify_deadlock(log);

      const std::string message =
          choose_message(log);

      const std::string description =
          choose_description(log);

      const std::string hint =
          choose_hint(log);

      const std::string secondaryHint =
          choose_secondary_hint(log);

      RuntimeLocation location =
          find_best_runtime_location(
              log,
              sourceFile);

      if (!location.valid() &&
          may_use_source_hint(kind))
      {
        location =
            find_best_runtime_location_or_source_hint(
                log,
                sourceFile,
                source_patterns_for_local_deadlock());
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

      print_hints_and_location(
          hint,
          secondaryHint,
          location);

      print_debug_log(log);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule>
  makeDeadlockRule()
  {
    return std::make_unique<DeadlockRule>();
  }
} // namespace vix::cli::errors::runtime
