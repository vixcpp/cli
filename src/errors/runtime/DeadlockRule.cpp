/**
 *
 *  @file DeadlockRule.cpp
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

    DeadlockKind classify_deadlock(const std::string &log)
    {
      if (icontains(log, "lock-order-inversion") ||
          icontains(log, "lock order inversion") ||
          icontains(log, "cycle in lock order graph"))
      {
        return DeadlockKind::LockOrderInversion;
      }

      if (icontains(log, "resource deadlock avoided") ||
          icontains(log, "edeadlk") ||
          icontains(log, "e deadlk"))
      {
        return DeadlockKind::ResourceDeadlockAvoided;
      }

      if (icontains(log, "self-deadlock") ||
          icontains(log, "self deadlock") ||
          icontains(log, "would deadlock"))
      {
        return DeadlockKind::SelfDeadlock;
      }

      if (icontains(log, "recursive") &&
          (icontains(log, "mutex") ||
           icontains(log, "lock")))
      {
        return DeadlockKind::RecursiveLock;
      }

      if (icontains(log, "mutex") &&
          (icontains(log, "cycle") ||
           icontains(log, "deadlock")))
      {
        return DeadlockKind::MutexCycle;
      }

      if (icontains(log, "ThreadSanitizer") &&
          icontains(log, "deadlock"))
      {
        return DeadlockKind::ThreadSanitizerDeadlock;
      }

      return DeadlockKind::GenericDeadlock;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_deadlock(log))
      {
      case DeadlockKind::LockOrderInversion:
        return "lock-order inversion detected";

      case DeadlockKind::ResourceDeadlockAvoided:
        return "resource deadlock avoided";

      case DeadlockKind::SelfDeadlock:
        return "thread tried to lock a mutex it already owns";

      case DeadlockKind::RecursiveLock:
        return "recursive mutex locking issue";

      case DeadlockKind::MutexCycle:
        return "mutex lock cycle detected";

      case DeadlockKind::ThreadSanitizerDeadlock:
        return "ThreadSanitizer detected a deadlock";

      case DeadlockKind::GenericDeadlock:
      default:
        return "deadlock";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_deadlock(log))
      {
      case DeadlockKind::LockOrderInversion:
        return "always acquire multiple mutexes in the same order, or use std::scoped_lock to lock them together";

      case DeadlockKind::ResourceDeadlockAvoided:
        return "a thread tried to lock a resource in a way that would deadlock; check nested locks and repeated lock() calls";

      case DeadlockKind::SelfDeadlock:
        return "avoid locking the same non-recursive mutex twice in the same thread";

      case DeadlockKind::RecursiveLock:
        return "use a single lock scope when possible; if recursion is required, use std::recursive_mutex intentionally";

      case DeadlockKind::MutexCycle:
        return "break the lock cycle by enforcing a strict global lock order";

      case DeadlockKind::ThreadSanitizerDeadlock:
        return "read the ThreadSanitizer lock graph below; it usually shows the conflicting lock order";

      case DeadlockKind::GenericDeadlock:
      default:
        return "lock shared resources in a consistent order and prefer std::scoped_lock when acquiring multiple mutexes";
      }
    }

    std::vector<std::string> source_patterns_for_deadlock()
    {
      return {
          "std::mutex",
          "std::recursive_mutex",
          "std::timed_mutex",
          "std::shared_mutex",
          "std::scoped_lock",
          "std::lock_guard",
          "std::unique_lock",
          "std::shared_lock",
          "std::lock(",
          ".lock(",
          ".try_lock(",
          ".unlock(",
          "pthread_mutex_lock",
          "pthread_mutex_trylock",
          "pthread_mutex_unlock",
      };
    }

    bool looks_like_deadlock_log(const std::string &log)
    {
      return icontains(log, "deadlock") ||
             icontains(log, "resource deadlock avoided") ||
             icontains(log, "edeadlk") ||
             icontains(log, "e deadlk") ||
             icontains(log, "std::system_error: resource deadlock avoided") ||
             icontains(log, "ThreadSanitizer: lock-order-inversion") ||
             icontains(log, "lock-order-inversion") ||
             icontains(log, "lock order inversion");
    }
  } // namespace

  class DeadlockRule final : public IRuntimeErrorRule
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
      const std::string message = choose_message(log);

      RuntimeLocation location =
          find_best_runtime_location(log, sourceFile);

      if (!location.valid())
      {
        location =
            find_best_runtime_location_or_source_hint(
                log,
                sourceFile,
                source_patterns_for_deadlock());
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
              "do not ignore the runtime log: deadlock reports often require comparing multiple lock stack traces",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 24);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeDeadlockRule()
  {
    return std::make_unique<DeadlockRule>();
  }
} // namespace vix::cli::errors::runtime
