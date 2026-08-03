/**
 *
 *  @file DetachedThreadLifetimeRule.cpp
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
    enum class DetachedThreadLifetimeKind
    {
      CapturedStackUseAfterScope,
      CapturedStackUseAfterReturn,
      UsedFreedMemory,
      DanglingReferenceCapture,
      DetachedWorkOutlivedOwner,
      GenericLifetimeBug,
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

    bool has_detached_thread_signal(
        const std::string &log)
    {
      return icontains(log, "detached thread") ||
             icontains(log, "thread was detached") ||
             icontains(log, "std::thread::detach") ||
             icontains(log, ".detach()") ||
             icontains(log, ".detach(") ||
             icontains(log, "detach()");
    }

    bool has_lifetime_signal(
        const std::string &log)
    {
      return icontains(log, "heap-use-after-free") ||
             icontains(log, "use-after-free") ||
             icontains(log, "stack-use-after-scope") ||
             icontains(log, "use-after-scope") ||
             icontains(log, "stack-use-after-return") ||
             icontains(log, "use-after-return") ||
             icontains(log, "dangling reference") ||
             icontains(log, "dangling pointer") ||
             icontains(log, "invalid memory access") ||
             icontains(log, "lifetime ended") ||
             icontains(log, "object was destroyed") ||
             icontains(log, "owner was destroyed");
    }

    bool has_reference_capture_signal(
        const std::string &log)
    {
      return icontains(log, "reference capture") ||
             icontains(log, "captured by reference") ||
             icontains(log, "lambda capture") ||
             icontains(log, "dangling reference") ||
             icontains(log, "capture [&]") ||
             icontains(log, "capture [this]");
    }

    DetachedThreadLifetimeKind classify_issue(
        const std::string &log)
    {
      if (icontains(log, "stack-use-after-scope") ||
          icontains(log, "use-after-scope"))
      {
        return DetachedThreadLifetimeKind::
            CapturedStackUseAfterScope;
      }

      if (icontains(log, "stack-use-after-return") ||
          icontains(log, "use-after-return"))
      {
        return DetachedThreadLifetimeKind::
            CapturedStackUseAfterReturn;
      }

      if (icontains(log, "heap-use-after-free") ||
          icontains(log, "use-after-free"))
      {
        return DetachedThreadLifetimeKind::
            UsedFreedMemory;
      }

      if (has_reference_capture_signal(log))
      {
        return DetachedThreadLifetimeKind::
            DanglingReferenceCapture;
      }

      if (icontains(log, "outlived its owner") ||
          icontains(log, "outlived the owner") ||
          icontains(log, "owner was destroyed") ||
          icontains(log, "object was destroyed") ||
          icontains(log, "lifetime ended"))
      {
        return DetachedThreadLifetimeKind::
            DetachedWorkOutlivedOwner;
      }

      return DetachedThreadLifetimeKind::
          GenericLifetimeBug;
    }

    std::string choose_message(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case DetachedThreadLifetimeKind::
          CapturedStackUseAfterScope:
        return "detached thread accessed expired local data";

      case DetachedThreadLifetimeKind::
          CapturedStackUseAfterReturn:
        return "detached thread used data after the function returned";

      case DetachedThreadLifetimeKind::
          UsedFreedMemory:
        return "detached thread accessed freed memory";

      case DetachedThreadLifetimeKind::
          DanglingReferenceCapture:
        return "detached thread captured a dangling reference";

      case DetachedThreadLifetimeKind::
          DetachedWorkOutlivedOwner:
        return "detached work outlived its owner";

      case DetachedThreadLifetimeKind::
          GenericLifetimeBug:
      default:
        return "detached thread lifetime error";
      }
    }

    std::string choose_description(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case DetachedThreadLifetimeKind::
          CapturedStackUseAfterScope:
        return "The thread continued using a local object after that object left its scope.";

      case DetachedThreadLifetimeKind::
          CapturedStackUseAfterReturn:
        return "The thread kept a pointer or reference to data owned by a function that had already returned.";

      case DetachedThreadLifetimeKind::
          UsedFreedMemory:
        return "The detached thread continued using memory after its owner released it.";

      case DetachedThreadLifetimeKind::
          DanglingReferenceCapture:
        return "A lambda captured data by reference, but the detached thread lived longer than the referenced object.";

      case DetachedThreadLifetimeKind::
          DetachedWorkOutlivedOwner:
        return "The owner object was destroyed while detached work was still running.";

      case DetachedThreadLifetimeKind::
          GenericLifetimeBug:
      default:
        return "Detached work continued after one of the objects it depends on was no longer valid.";
      }
    }

    std::string choose_hint(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case DetachedThreadLifetimeKind::
          CapturedStackUseAfterScope:
        return "capture required values by value, move owned data into the thread, or join before local data leaves scope";

      case DetachedThreadLifetimeKind::
          CapturedStackUseAfterReturn:
        return "do not let detached work retain pointers or references to local variables after the function returns";

      case DetachedThreadLifetimeKind::
          UsedFreedMemory:
        return "give the thread ownership of its data, or stop and join it before destroying shared state";

      case DetachedThreadLifetimeKind::
          DanglingReferenceCapture:
        return "avoid [&] and unsafe [this] captures in detached threads; capture values explicitly or use safe shared ownership";

      case DetachedThreadLifetimeKind::
          DetachedWorkOutlivedOwner:
        return "replace detach() with std::jthread or another RAII shutdown mechanism when the work depends on an owner";

      case DetachedThreadLifetimeKind::
          GenericLifetimeBug:
      default:
        return "make detached work own everything it needs for its complete lifetime";
      }
    }

    std::string choose_secondary_hint(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case DetachedThreadLifetimeKind::
          CapturedStackUseAfterScope:
      case DetachedThreadLifetimeKind::
          CapturedStackUseAfterReturn:
      case DetachedThreadLifetimeKind::
          UsedFreedMemory:
        return "run with VIX_LOG_LEVEL=debug to compare the capture, destruction, and invalid access traces";

      case DetachedThreadLifetimeKind::
          DanglingReferenceCapture:
      case DetachedThreadLifetimeKind::
          DetachedWorkOutlivedOwner:
      case DetachedThreadLifetimeKind::
          GenericLifetimeBug:
      default:
        return {};
      }
    }

    bool may_use_source_hint(
        DetachedThreadLifetimeKind kind)
    {
      /*
       * Source scanning is useful when the likely problem is the
       * capture or detach operation itself.
       *
       * For sanitizer-reported memory errors, the real runtime
       * frame is safer than guessing from the source.
       */
      switch (kind)
      {
      case DetachedThreadLifetimeKind::
          DanglingReferenceCapture:
      case DetachedThreadLifetimeKind::
          DetachedWorkOutlivedOwner:
      case DetachedThreadLifetimeKind::
          GenericLifetimeBug:
        return true;

      case DetachedThreadLifetimeKind::
          CapturedStackUseAfterScope:
      case DetachedThreadLifetimeKind::
          CapturedStackUseAfterReturn:
      case DetachedThreadLifetimeKind::
          UsedFreedMemory:
      default:
        return false;
      }
    }

    std::vector<std::string>
    source_patterns_for_detached_thread(
        DetachedThreadLifetimeKind kind)
    {
      switch (kind)
      {
      case DetachedThreadLifetimeKind::
          DanglingReferenceCapture:
        return {
            "[&]",
            "[this]",
            "std::ref(",
            "std::cref(",
            ".detach()",
            ".detach(",
        };

      case DetachedThreadLifetimeKind::
          DetachedWorkOutlivedOwner:
        return {
            "[this]",
            ".detach()",
            ".detach(",
        };

      case DetachedThreadLifetimeKind::
          GenericLifetimeBug:
      default:
        return {
            ".detach()",
            ".detach(",
            "[&]",
            "[this]",
            "std::ref(",
            "std::cref(",
        };
      }
    }

    bool looks_like_detached_thread_lifetime_log(
        const std::string &log)
    {
      const bool detachedThread =
          has_detached_thread_signal(log);

      if (!detachedThread)
        return false;

      return has_lifetime_signal(log) ||
             has_reference_capture_signal(log) ||
             icontains(log, "outlived its owner") ||
             icontains(log, "outlived the owner");
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
       * Do not use sourceFile as an automatic fallback.
       * A filename alone is not a useful lifetime-error location.
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
       * Sanitizer reports may contain the invalid access,
       * allocation, destruction and thread-creation stacks.
       */
      print_runtime_log_excerpt(
          log,
          24);
    }
  } // namespace

  class DetachedThreadLifetimeRule final
      : public IRuntimeErrorRule
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
      const DetachedThreadLifetimeKind kind =
          classify_issue(log);

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
                source_patterns_for_detached_thread(kind));
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
  makeDetachedThreadLifetimeRule()
  {
    return std::make_unique<
        DetachedThreadLifetimeRule>();
  }
} // namespace vix::cli::errors::runtime
