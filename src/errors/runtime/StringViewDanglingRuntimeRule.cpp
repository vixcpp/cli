/**
 *
 *  @file StringViewDanglingRuntimeRule.cpp
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
    enum class StringViewLifetimeKind
    {
      OutlivedLocalString,
      UseAfterReturn,
      PointsToFreedMemory,
      PointsToTemporary,
      InvalidatedByStringMutation,
      InvalidatedByMove,
      ReturnedDanglingView,
      GenericDanglingView,
    };

    StringViewLifetimeKind classify_issue(const std::string &log)
    {
      if (icontains(log, "use-after-return"))
      {
        return StringViewLifetimeKind::UseAfterReturn;
      }

      if (icontains(log, "stack-use-after-scope") ||
          icontains(log, "use-after-scope"))
      {
        return StringViewLifetimeKind::OutlivedLocalString;
      }

      if (icontains(log, "heap-use-after-free") ||
          icontains(log, "use-after-free"))
      {
        return StringViewLifetimeKind::PointsToFreedMemory;
      }

      if (icontains(log, "temporary") ||
          icontains(log, "temporary object") ||
          icontains(log, "rvalue"))
      {
        return StringViewLifetimeKind::PointsToTemporary;
      }

      if (icontains(log, "reallocation") ||
          icontains(log, "reallocated") ||
          icontains(log, "reserve") ||
          icontains(log, "resize") ||
          icontains(log, "clear") ||
          icontains(log, "append") ||
          icontains(log, "operator+="))
      {
        return StringViewLifetimeKind::InvalidatedByStringMutation;
      }

      if (icontains(log, "moved-from") ||
          icontains(log, "moved from") ||
          icontains(log, "std::move"))
      {
        return StringViewLifetimeKind::InvalidatedByMove;
      }

      if (icontains(log, "return") &&
          (icontains(log, "string_view") ||
           icontains(log, "basic_string_view")))
      {
        return StringViewLifetimeKind::ReturnedDanglingView;
      }

      return StringViewLifetimeKind::GenericDanglingView;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case StringViewLifetimeKind::OutlivedLocalString:
        return "std::string_view outlived local string data";

      case StringViewLifetimeKind::UseAfterReturn:
        return "std::string_view points to data from a returned function";

      case StringViewLifetimeKind::PointsToFreedMemory:
        return "std::string_view points to freed memory";

      case StringViewLifetimeKind::PointsToTemporary:
        return "std::string_view points to temporary string data";

      case StringViewLifetimeKind::InvalidatedByStringMutation:
        return "std::string_view invalidated by string mutation";

      case StringViewLifetimeKind::InvalidatedByMove:
        return "std::string_view points to moved-from string data";

      case StringViewLifetimeKind::ReturnedDanglingView:
        return "function returned a dangling std::string_view";

      case StringViewLifetimeKind::GenericDanglingView:
      default:
        return "dangling std::string_view";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case StringViewLifetimeKind::OutlivedLocalString:
        return "return std::string when ownership is needed, or ensure the local string outlives the std::string_view";

      case StringViewLifetimeKind::UseAfterReturn:
        return "do not return std::string_view pointing to local std::string or temporary storage";

      case StringViewLifetimeKind::PointsToFreedMemory:
        return "avoid keeping std::string_view after the owning string is destroyed";

      case StringViewLifetimeKind::PointsToTemporary:
        return "store the string in a stable std::string before creating std::string_view";

      case StringViewLifetimeKind::InvalidatedByStringMutation:
        return "recreate the std::string_view after modifying the owning string";

      case StringViewLifetimeKind::InvalidatedByMove:
        return "do not keep std::string_view into a string after that string has been moved";

      case StringViewLifetimeKind::ReturnedDanglingView:
        return "return std::string instead, or return std::string_view only when the referenced storage is guaranteed to outlive the caller";

      case StringViewLifetimeKind::GenericDanglingView:
      default:
        return "do not keep std::string_view to temporary, local, destroyed, moved, or reallocated string storage";
      }
    }

    std::vector<std::string> source_patterns_for_string_view_lifetime()
    {
      return {
          "std::string_view",
          "string_view",
          "basic_string_view",
          ".substr(",
          "substr(",
          ".data()",
          "data()",
          ".c_str()",
          "c_str()",
          "std::string",
          "string ",
          "std::move(",
          ".clear(",
          "clear(",
          ".resize(",
          "resize(",
          ".reserve(",
          "reserve(",
          ".append(",
          "append(",
          ".push_back(",
          "push_back(",
          "+=",
          "return",
      };
    }

    bool looks_like_string_view_lifetime_log(const std::string &log)
    {
      const bool hasStringViewSignal =
          icontains(log, "string_view") ||
          icontains(log, "basic_string_view") ||
          icontains(log, "std::string_view");

      const bool hasLifetimeSignal =
          icontains(log, "dangling") ||
          icontains(log, "lifetime") ||
          icontains(log, "invalid") ||
          icontains(log, "stack-use-after-scope") ||
          icontains(log, "use-after-scope") ||
          icontains(log, "use-after-return") ||
          icontains(log, "heap-use-after-free") ||
          icontains(log, "use-after-free") ||
          icontains(log, "temporary") ||
          icontains(log, "reallocation") ||
          icontains(log, "reallocated") ||
          icontains(log, "moved-from") ||
          icontains(log, "moved from");

      return hasStringViewSignal && hasLifetimeSignal;
    }
  } // namespace

  class StringViewDanglingRuntimeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_string_view_lifetime_log(log);
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
                source_patterns_for_string_view_lifetime());
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
              "do not ignore the runtime log: string_view bugs often require comparing the view creation site and the invalid use site",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 22);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeStringViewDanglingRuntimeRule()
  {
    return std::make_unique<StringViewDanglingRuntimeRule>();
  }
} // namespace vix::cli::errors::runtime
