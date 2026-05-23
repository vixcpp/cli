/**
 *
 *  @file ThreadCreationFailureRule.cpp
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
    enum class ThreadCreationFailureKind
    {
      ResourceLimitReached,
      PthreadCreateFailed,
      OutOfMemory,
      StackSizeFailure,
      TooManyThreads,
      SystemErrorThreadFailure,
      GenericThreadCreationFailure,
    };

    ThreadCreationFailureKind classify_issue(const std::string &log)
    {
      if (icontains(log, "resource temporarily unavailable") ||
          icontains(log, "eagain") ||
          icontains(log, "temporarily unavailable"))
      {
        return ThreadCreationFailureKind::ResourceLimitReached;
      }

      if (icontains(log, "pthread_create"))
      {
        return ThreadCreationFailureKind::PthreadCreateFailed;
      }

      if (icontains(log, "cannot allocate memory") ||
          icontains(log, "out of memory") ||
          icontains(log, "bad_alloc") ||
          icontains(log, "enomem"))
      {
        return ThreadCreationFailureKind::OutOfMemory;
      }

      if (icontains(log, "stack size") ||
          icontains(log, "thread stack") ||
          icontains(log, "pthread_attr_setstacksize"))
      {
        return ThreadCreationFailureKind::StackSizeFailure;
      }

      if (icontains(log, "too many threads") ||
          icontains(log, "thread limit") ||
          icontains(log, "max user processes") ||
          icontains(log, "ulimit"))
      {
        return ThreadCreationFailureKind::TooManyThreads;
      }

      if (icontains(log, "std::system_error") &&
          (icontains(log, "thread") ||
           icontains(log, "create")))
      {
        return ThreadCreationFailureKind::SystemErrorThreadFailure;
      }

      return ThreadCreationFailureKind::GenericThreadCreationFailure;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case ThreadCreationFailureKind::ResourceLimitReached:
        return "thread resource limit reached";

      case ThreadCreationFailureKind::PthreadCreateFailed:
        return "pthread_create failed";

      case ThreadCreationFailureKind::OutOfMemory:
        return "not enough memory to create thread";

      case ThreadCreationFailureKind::StackSizeFailure:
        return "invalid or excessive thread stack size";

      case ThreadCreationFailureKind::TooManyThreads:
        return "too many threads created";

      case ThreadCreationFailureKind::SystemErrorThreadFailure:
        return "std::thread creation failed";

      case ThreadCreationFailureKind::GenericThreadCreationFailure:
      default:
        return "thread creation failed";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case ThreadCreationFailureKind::ResourceLimitReached:
        return "reduce the number of concurrent threads or use a bounded thread pool";

      case ThreadCreationFailureKind::PthreadCreateFailed:
        return "check thread limits, available memory, stack size, and thread count";

      case ThreadCreationFailureKind::OutOfMemory:
        return "reduce thread count or memory usage; each thread needs stack memory";

      case ThreadCreationFailureKind::StackSizeFailure:
        return "use a smaller valid thread stack size or avoid custom pthread stack configuration";

      case ThreadCreationFailureKind::TooManyThreads:
        return "avoid creating one thread per task; use a thread pool or async queue";

      case ThreadCreationFailureKind::SystemErrorThreadFailure:
        return "std::thread could not start; check system limits and excessive thread creation";

      case ThreadCreationFailureKind::GenericThreadCreationFailure:
      default:
        return "check system thread limits, available memory, and excessive thread creation";
      }
    }

    std::vector<std::string> source_patterns_for_thread_creation()
    {
      return {
          "std::thread",
          "std::jthread",
          "thread(",
          "jthread(",
          ".detach(",
          ".detach()",
          ".join(",
          ".join()",
          "pthread_create",
          "emplace_back(",
          "push_back(",
          "std::async",
          "async(",
          "RuntimeExecutor",
          "ThreadPool",
          "threadpool",
      };
    }

    bool looks_like_thread_creation_failure_log(const std::string &log)
    {
      const bool systemThreadFailure =
          icontains(log, "std::system_error") &&
          (icontains(log, "thread") ||
           icontains(log, "resource temporarily unavailable") ||
           icontains(log, "operation not permitted"));

      return systemThreadFailure ||
             icontains(log, "resource temporarily unavailable") ||
             icontains(log, "thread constructor failed") ||
             icontains(log, "failed to create thread") ||
             icontains(log, "pthread_create") ||
             icontains(log, "too many threads") ||
             icontains(log, "thread resource limit") ||
             icontains(log, "thread limit reached");
    }
  } // namespace

  class ThreadCreationFailureRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_thread_creation_failure_log(log);
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
                source_patterns_for_thread_creation());
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
              "do not ignore the runtime log: thread creation failures often include the OS error that explains the limit",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeThreadCreationFailureRule()
  {
    return std::make_unique<ThreadCreationFailureRule>();
  }
} // namespace vix::cli::errors::runtime
