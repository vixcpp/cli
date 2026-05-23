/**
 *
 *  @file SpanLifetimeRule.cpp
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
    enum class SpanLifetimeKind
    {
      OutlivedLocalStorage,
      UseAfterReturn,
      PointsToFreedMemory,
      InvalidatedByReallocation,
      InvalidatedByResizeOrClear,
      ReturnedDanglingSpan,
      GenericDanglingSpan,
    };

    SpanLifetimeKind classify_issue(const std::string &log)
    {
      if (icontains(log, "use-after-return"))
      {
        return SpanLifetimeKind::UseAfterReturn;
      }

      if (icontains(log, "stack-use-after-scope") ||
          icontains(log, "use-after-scope"))
      {
        return SpanLifetimeKind::OutlivedLocalStorage;
      }

      if (icontains(log, "heap-use-after-free") ||
          icontains(log, "use-after-free"))
      {
        return SpanLifetimeKind::PointsToFreedMemory;
      }

      if (icontains(log, "reallocation") ||
          icontains(log, "reallocated") ||
          icontains(log, "reserve") ||
          icontains(log, "push_back") ||
          icontains(log, "emplace_back"))
      {
        return SpanLifetimeKind::InvalidatedByReallocation;
      }

      if (icontains(log, "resize") ||
          icontains(log, "clear") ||
          icontains(log, "shrink_to_fit"))
      {
        return SpanLifetimeKind::InvalidatedByResizeOrClear;
      }

      if (icontains(log, "return") &&
          (icontains(log, "span") || icontains(log, "std::span")))
      {
        return SpanLifetimeKind::ReturnedDanglingSpan;
      }

      return SpanLifetimeKind::GenericDanglingSpan;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case SpanLifetimeKind::OutlivedLocalStorage:
        return "std::span outlived local storage";

      case SpanLifetimeKind::UseAfterReturn:
        return "std::span points to data from a returned function";

      case SpanLifetimeKind::PointsToFreedMemory:
        return "std::span points to freed memory";

      case SpanLifetimeKind::InvalidatedByReallocation:
        return "std::span invalidated by container reallocation";

      case SpanLifetimeKind::InvalidatedByResizeOrClear:
        return "std::span invalidated by container resize or clear";

      case SpanLifetimeKind::ReturnedDanglingSpan:
        return "function returned a dangling std::span";

      case SpanLifetimeKind::GenericDanglingSpan:
      default:
        return "dangling std::span";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case SpanLifetimeKind::OutlivedLocalStorage:
        return "ensure the local array, buffer, or object referenced by std::span outlives the span";

      case SpanLifetimeKind::UseAfterReturn:
        return "do not return std::span pointing to local variables or temporary storage";

      case SpanLifetimeKind::PointsToFreedMemory:
        return "avoid keeping std::span after the owning allocation or container is destroyed";

      case SpanLifetimeKind::InvalidatedByReallocation:
        return "recreate the std::span after vector reserve, push_back, emplace_back, or any operation that may reallocate";

      case SpanLifetimeKind::InvalidatedByResizeOrClear:
        return "recreate or discard the std::span after resize, clear, erase, or shrink_to_fit";

      case SpanLifetimeKind::ReturnedDanglingSpan:
        return "return an owning container instead, or ensure the referenced storage outlives the returned span";

      case SpanLifetimeKind::GenericDanglingSpan:
      default:
        return "ensure the array, vector, string, or buffer referenced by std::span stays alive and stable";
      }
    }

    std::vector<std::string> source_patterns_for_span_lifetime()
    {
      return {
          "std::span",
          "span<",
          "span ",
          "std::as_bytes",
          "std::as_writable_bytes",
          ".data()",
          "data()",
          ".size()",
          ".resize(",
          "resize(",
          ".reserve(",
          "reserve(",
          ".clear(",
          "clear(",
          ".erase(",
          "erase(",
          ".push_back(",
          "push_back(",
          ".emplace_back(",
          "emplace_back(",
          ".shrink_to_fit(",
          "shrink_to_fit(",
          "delete",
          "delete[]",
          "free(",
          "return",
      };
    }

    bool looks_like_span_lifetime_log(const std::string &log)
    {
      const bool hasSpanSignal =
          icontains(log, "span") ||
          icontains(log, "std::span");

      const bool hasLifetimeSignal =
          icontains(log, "dangling") ||
          icontains(log, "lifetime") ||
          icontains(log, "invalid") ||
          icontains(log, "stack-use-after-scope") ||
          icontains(log, "use-after-scope") ||
          icontains(log, "use-after-return") ||
          icontains(log, "heap-use-after-free") ||
          icontains(log, "use-after-free") ||
          icontains(log, "outlived") ||
          icontains(log, "reallocation") ||
          icontains(log, "reallocated");

      return hasSpanSignal && hasLifetimeSignal;
    }
  } // namespace

  class SpanLifetimeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_span_lifetime_log(log);
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
                source_patterns_for_span_lifetime());
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
              "do not ignore the runtime log: span lifetime bugs often require comparing the span creation site and the invalid use site",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 22);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeSpanLifetimeRule()
  {
    return std::make_unique<SpanLifetimeRule>();
  }
} // namespace vix::cli::errors::runtime
