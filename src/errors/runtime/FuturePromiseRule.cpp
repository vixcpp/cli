/**
 *
 *  @file FuturePromiseRule.cpp
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
    enum class FuturePromiseKind
    {
      BrokenPromise,
      PromiseAlreadySatisfied,
      FutureAlreadyRetrieved,
      NoAssociatedState,
      PackagedTaskAlreadyStarted,
      PackagedTaskNoState,
      AsyncFailure,
      GenericFutureError,
    };

    FuturePromiseKind classify_issue(const std::string &log)
    {
      if (icontains(log, "broken promise"))
        return FuturePromiseKind::BrokenPromise;

      if (icontains(log, "promise already satisfied") ||
          icontains(log, "already satisfied"))
      {
        return FuturePromiseKind::PromiseAlreadySatisfied;
      }

      if (icontains(log, "future already retrieved") ||
          icontains(log, "future_already_retrieved"))
      {
        return FuturePromiseKind::FutureAlreadyRetrieved;
      }

      if (icontains(log, "no associated state") ||
          icontains(log, "no_state"))
      {
        if (icontains(log, "packaged_task"))
          return FuturePromiseKind::PackagedTaskNoState;

        return FuturePromiseKind::NoAssociatedState;
      }

      if (icontains(log, "packaged_task") &&
          (icontains(log, "already started") ||
           icontains(log, "task already started")))
      {
        return FuturePromiseKind::PackagedTaskAlreadyStarted;
      }

      if (icontains(log, "std::async") ||
          icontains(log, "async"))
      {
        return FuturePromiseKind::AsyncFailure;
      }

      return FuturePromiseKind::GenericFutureError;
    }

    std::string choose_title(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case FuturePromiseKind::BrokenPromise:
        return "runtime error: broken promise";

      case FuturePromiseKind::PromiseAlreadySatisfied:
        return "runtime error: promise already satisfied";

      case FuturePromiseKind::FutureAlreadyRetrieved:
        return "runtime error: future already retrieved";

      case FuturePromiseKind::NoAssociatedState:
        return "runtime error: future/promise has no associated state";

      case FuturePromiseKind::PackagedTaskAlreadyStarted:
        return "runtime error: packaged_task already started";

      case FuturePromiseKind::PackagedTaskNoState:
        return "runtime error: packaged_task has no associated state";

      case FuturePromiseKind::AsyncFailure:
        return "runtime error: async future failure";

      case FuturePromiseKind::GenericFutureError:
      default:
        return "runtime error: future/promise misuse";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case FuturePromiseKind::BrokenPromise:
        return "set a value or exception before destroying the promise, or keep the promise alive until fulfillment";

      case FuturePromiseKind::PromiseAlreadySatisfied:
        return "call set_value() or set_exception() only once for the same promise";

      case FuturePromiseKind::FutureAlreadyRetrieved:
        return "call get_future() only once for the same promise; store and reuse the returned future";

      case FuturePromiseKind::NoAssociatedState:
        return "avoid using moved-from futures/promises and check valid() before get() or wait()";

      case FuturePromiseKind::PackagedTaskAlreadyStarted:
        return "a packaged_task can only run once; create a new task for each execution";

      case FuturePromiseKind::PackagedTaskNoState:
        return "avoid invoking a moved-from or default-constructed packaged_task";

      case FuturePromiseKind::AsyncFailure:
        return "inspect the async task body; exceptions thrown inside async are rethrown by future.get()";

      case FuturePromiseKind::GenericFutureError:
      default:
        return "check future/promise ownership and ensure the shared state is consumed or fulfilled only once";
      }
    }

    std::vector<std::string> source_patterns_for_future_promise()
    {
      return {
          "std::promise",
          "std::future",
          "std::shared_future",
          "std::packaged_task",
          "std::async",
          ".get_future(",
          ".get_future()",
          ".set_value(",
          ".set_exception(",
          ".get(",
          ".wait(",
          ".wait_for(",
          ".wait_until(",
          ".valid(",
          ".valid()",
      };
    }

    bool looks_like_future_promise_log(const std::string &log)
    {
      return icontains(log, "std::future_error") ||
             icontains(log, "future_error") ||
             icontains(log, "broken promise") ||
             icontains(log, "promise already satisfied") ||
             icontains(log, "already satisfied") ||
             icontains(log, "future already retrieved") ||
             icontains(log, "future_already_retrieved") ||
             icontains(log, "no associated state") ||
             icontains(log, "no_state") ||
             icontains(log, "packaged_task");
    }
  } // namespace

  class FuturePromiseRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_future_promise_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      RuntimeLocation location =
          find_best_runtime_location(log, sourceFile);

      if (!location.valid())
      {
        location =
            find_best_runtime_location_or_source_hint(
                log,
                sourceFile,
                source_patterns_for_future_promise());
      }

      const std::string title = choose_title(log);
      const std::string message =
          title.rfind("runtime error: ", 0) == 0
              ? title.substr(std::string("runtime error: ").size())
              : title;

      std::cerr << RED
                << title
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
              "do not ignore the runtime log: future errors often show the exact std::future_error reason in what()",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 18);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeFuturePromiseRule()
  {
    return std::make_unique<FuturePromiseRule>();
  }
} // namespace vix::cli::errors::runtime
