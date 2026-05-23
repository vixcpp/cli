/**
 *
 *  @file DetachedThreadLifetimeRule.cpp
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
    enum class DetachedThreadLifetimeKind
    {
      CapturedStackUseAfterScope,
      CapturedStackUseAfterReturn,
      UsedFreedMemory,
      DanglingReferenceCapture,
      DetachedWorkOutlivedOwner,
      GenericLifetimeBug,
    };

    DetachedThreadLifetimeKind classify_issue(const std::string &log)
    {
      if (icontains(log, "stack-use-after-scope") ||
          icontains(log, "use-after-scope"))
      {
        return DetachedThreadLifetimeKind::CapturedStackUseAfterScope;
      }

      if (icontains(log, "use-after-return"))
      {
        return DetachedThreadLifetimeKind::CapturedStackUseAfterReturn;
      }

      if (icontains(log, "heap-use-after-free") ||
          icontains(log, "use-after-free"))
      {
        return DetachedThreadLifetimeKind::UsedFreedMemory;
      }

      if ((icontains(log, "lambda") ||
           icontains(log, "capture") ||
           icontains(log, "reference")) &&
          (icontains(log, "detach") ||
           icontains(log, "detached thread")))
      {
        return DetachedThreadLifetimeKind::DanglingReferenceCapture;
      }

      if (icontains(log, "detached thread") &&
          (icontains(log, "lifetime") ||
           icontains(log, "dangling") ||
           icontains(log, "owner") ||
           icontains(log, "destroyed")))
      {
        return DetachedThreadLifetimeKind::DetachedWorkOutlivedOwner;
      }

      return DetachedThreadLifetimeKind::GenericLifetimeBug;
    }

    std::string choose_title(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case DetachedThreadLifetimeKind::CapturedStackUseAfterScope:
        return "runtime error: detached thread captured expired stack data";

      case DetachedThreadLifetimeKind::CapturedStackUseAfterReturn:
        return "runtime error: detached thread used data after function returned";

      case DetachedThreadLifetimeKind::UsedFreedMemory:
        return "runtime error: detached thread used freed memory";

      case DetachedThreadLifetimeKind::DanglingReferenceCapture:
        return "runtime error: detached thread captured dangling reference";

      case DetachedThreadLifetimeKind::DetachedWorkOutlivedOwner:
        return "runtime error: detached work outlived its owner";

      case DetachedThreadLifetimeKind::GenericLifetimeBug:
      default:
        return "runtime error: detached thread lifetime bug";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case DetachedThreadLifetimeKind::CapturedStackUseAfterScope:
        return "capture stack data by value, move owned data into the thread, or join before local state leaves scope";

      case DetachedThreadLifetimeKind::CapturedStackUseAfterReturn:
        return "do not let detached work reference local variables after the function returns";

      case DetachedThreadLifetimeKind::UsedFreedMemory:
        return "ensure detached work owns its data safely, or stop and join the thread before destroying shared state";

      case DetachedThreadLifetimeKind::DanglingReferenceCapture:
        return "avoid [&] captures in detached threads; capture required values explicitly by value or use shared ownership";

      case DetachedThreadLifetimeKind::DetachedWorkOutlivedOwner:
        return "give detached work independent ownership, or replace detach() with joinable std::jthread/RAII shutdown";

      case DetachedThreadLifetimeKind::GenericLifetimeBug:
      default:
        return "avoid capturing local state by reference in detached threads unless its lifetime is guaranteed";
      }
    }

    std::vector<std::string> source_patterns_for_detached_thread()
    {
      return {
          ".detach()",
          ".detach(",
          "std::thread",
          "std::jthread",
          "std::async",
          "[&]",
          "[this]",
          "std::ref(",
          "std::cref(",
          "shared_ptr",
          "weak_ptr",
          "use-after-free",
          "use-after-scope",
          "use-after-return",
      };
    }

    bool looks_like_detached_thread_lifetime_log(const std::string &log)
    {
      const bool hasDetachedThreadSignal =
          icontains(log, "detach") ||
          icontains(log, "detached thread") ||
          icontains(log, ".detach");

      const bool hasLifetimeSignal =
          icontains(log, "use-after-free") ||
          icontains(log, "heap-use-after-free") ||
          icontains(log, "stack-use-after-scope") ||
          icontains(log, "use-after-scope") ||
          icontains(log, "use-after-return") ||
          icontains(log, "dangling") ||
          icontains(log, "invalid memory") ||
          icontains(log, "lifetime");

      const bool hasCaptureSignal =
          (icontains(log, "lambda") ||
           icontains(log, "capture") ||
           icontains(log, "reference")) &&
          hasDetachedThreadSignal;

      return (hasDetachedThreadSignal && hasLifetimeSignal) ||
             hasCaptureSignal;
    }
  } // namespace

  class DetachedThreadLifetimeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_detached_thread_lifetime_log(log);
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
                source_patterns_for_detached_thread());
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
              "do not ignore the runtime log: lifetime bugs often require comparing the allocation, capture, and use sites",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 24);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeDetachedThreadLifetimeRule()
  {
    return std::make_unique<DetachedThreadLifetimeRule>();
  }
} // namespace vix::cli::errors::runtime
