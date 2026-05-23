/**
 *
 *  @file MutexMisuseRule.cpp
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
    enum class MutexMisuseKind
    {
      ResourceDeadlockAvoided,
      UnlockNotOwned,
      InvalidMutexState,
      DestroyedMutex,
      DoubleLock,
      LockOrderIssue,
      PthreadMutexFailure,
      GenericMisuse,
    };

    MutexMisuseKind classify_mutex_issue(const std::string &log)
    {
      if (icontains(log, "resource deadlock avoided") ||
          icontains(log, "edeadlk") ||
          icontains(log, "e deadlk"))
      {
        return MutexMisuseKind::ResourceDeadlockAvoided;
      }

      if (icontains(log, "operation not permitted") ||
          icontains(log, "eperm") ||
          (icontains(log, "unlock") &&
           (icontains(log, "not owner") ||
            icontains(log, "not owned") ||
            icontains(log, "different thread"))))
      {
        return MutexMisuseKind::UnlockNotOwned;
      }

      if (icontains(log, "destroyed mutex") ||
          icontains(log, "mutex destroyed") ||
          (icontains(log, "destroy") && icontains(log, "mutex")))
      {
        return MutexMisuseKind::DestroyedMutex;
      }

      if (icontains(log, "invalid argument") ||
          icontains(log, "einval") ||
          icontains(log, "invalid mutex") ||
          icontains(log, "uninitialized mutex"))
      {
        return MutexMisuseKind::InvalidMutexState;
      }

      if ((icontains(log, "lock") || icontains(log, "mutex")) &&
          (icontains(log, "already locked") ||
           icontains(log, "double lock") ||
           icontains(log, "recursive lock")))
      {
        return MutexMisuseKind::DoubleLock;
      }

      if (icontains(log, "lock-order-inversion") ||
          icontains(log, "lock order inversion") ||
          icontains(log, "cycle in lock order"))
      {
        return MutexMisuseKind::LockOrderIssue;
      }

      if (icontains(log, "pthread_mutex"))
      {
        return MutexMisuseKind::PthreadMutexFailure;
      }

      return MutexMisuseKind::GenericMisuse;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_mutex_issue(log))
      {
      case MutexMisuseKind::ResourceDeadlockAvoided:
        return "mutex deadlock";

      case MutexMisuseKind::UnlockNotOwned:
        return "invalid mutex unlock";

      case MutexMisuseKind::InvalidMutexState:
        return "invalid mutex state";

      case MutexMisuseKind::DestroyedMutex:
        return "mutex used after destruction";

      case MutexMisuseKind::DoubleLock:
        return "mutex locked twice by the same thread";

      case MutexMisuseKind::LockOrderIssue:
        return "mutex lock-order inversion";

      case MutexMisuseKind::PthreadMutexFailure:
        return "pthread mutex misuse";

      case MutexMisuseKind::GenericMisuse:
      default:
        return "mutex misuse";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_mutex_issue(log))
      {
      case MutexMisuseKind::ResourceDeadlockAvoided:
        return "avoid locking the same non-recursive mutex twice in the same thread";

      case MutexMisuseKind::UnlockNotOwned:
        return "only unlock a mutex owned by the current thread and prefer RAII locks";

      case MutexMisuseKind::InvalidMutexState:
        return "check mutex lifetime and avoid using an uninitialized or destroyed mutex";

      case MutexMisuseKind::DestroyedMutex:
        return "stop and join all worker threads before destroying the object that owns the mutex";

      case MutexMisuseKind::DoubleLock:
        return "do not call lock() twice on the same std::mutex; use std::recursive_mutex only when recursion is intentional";

      case MutexMisuseKind::LockOrderIssue:
        return "lock multiple mutexes in a consistent order or use std::scoped_lock";

      case MutexMisuseKind::PthreadMutexFailure:
        return "check pthread mutex initialization, ownership, lock/unlock order, and destruction timing";

      case MutexMisuseKind::GenericMisuse:
      default:
        return "check double lock, invalid unlock, destroyed mutex access, and inconsistent lock usage";
      }
    }

    std::vector<std::string> source_patterns_for_mutex_misuse()
    {
      return {
          "std::mutex",
          "std::recursive_mutex",
          "std::timed_mutex",
          "std::shared_mutex",
          "std::lock_guard",
          "std::unique_lock",
          "std::scoped_lock",
          "std::shared_lock",
          ".lock(",
          ".try_lock(",
          ".unlock(",
          "std::lock(",
          "pthread_mutex_lock",
          "pthread_mutex_trylock",
          "pthread_mutex_unlock",
          "pthread_mutex_destroy",
          "pthread_mutex_init",
      };
    }

    bool looks_like_mutex_misuse_log(const std::string &log)
    {
      const bool explicitMutexError =
          icontains(log, "resource deadlock avoided") ||
          icontains(log, "operation not permitted") ||
          icontains(log, "pthread_mutex") ||
          icontains(log, "edeadlk") ||
          icontains(log, "eperm") ||
          icontains(log, "einval");

      const bool genericMutexError =
          icontains(log, "mutex") &&
          (icontains(log, "deadlock") ||
           icontains(log, "unlock") ||
           icontains(log, "lock") ||
           icontains(log, "invalid argument") ||
           icontains(log, "invalid") ||
           icontains(log, "destroyed") ||
           icontains(log, "uninitialized") ||
           icontains(log, "not owner") ||
           icontains(log, "not owned"));

      return explicitMutexError || genericMutexError;
    }
  } // namespace

  class MutexMisuseRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_mutex_misuse_log(log);
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
                source_patterns_for_mutex_misuse());
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
              "do not ignore the runtime log: mutex errors often require checking ownership and lock/unlock order",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeMutexMisuseRule()
  {
    return std::make_unique<MutexMisuseRule>();
  }
} // namespace vix::cli::errors::runtime
