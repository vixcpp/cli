/**
 *
 *  @file EmptyContainerFrontBackRule.cpp
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
    enum class EmptyContainerAccessKind
    {
      Front,
      Back,
      PopFront,
      PopBack,
      Top,
      DereferenceBeginEnd,
      Generic,
    };

    EmptyContainerAccessKind classify_access(const std::string &log)
    {
      if (icontains(log, "pop_front"))
        return EmptyContainerAccessKind::PopFront;

      if (icontains(log, "pop_back"))
        return EmptyContainerAccessKind::PopBack;

      if (icontains(log, "front"))
        return EmptyContainerAccessKind::Front;

      if (icontains(log, "back"))
        return EmptyContainerAccessKind::Back;

      if (icontains(log, "top"))
        return EmptyContainerAccessKind::Top;

      if ((icontains(log, "begin") && icontains(log, "end")) ||
          icontains(log, "past-the-end") ||
          icontains(log, "past the end"))
      {
        return EmptyContainerAccessKind::DereferenceBeginEnd;
      }

      return EmptyContainerAccessKind::Generic;
    }

    std::string choose_title(const std::string &log)
    {
      switch (classify_access(log))
      {
      case EmptyContainerAccessKind::Front:
        return "runtime error: front() on empty container";

      case EmptyContainerAccessKind::Back:
        return "runtime error: back() on empty container";

      case EmptyContainerAccessKind::PopFront:
        return "runtime error: pop_front() on empty container";

      case EmptyContainerAccessKind::PopBack:
        return "runtime error: pop_back() on empty container";

      case EmptyContainerAccessKind::Top:
        return "runtime error: top() on empty container";

      case EmptyContainerAccessKind::DereferenceBeginEnd:
        return "runtime error: dereferencing empty container iterator";

      case EmptyContainerAccessKind::Generic:
      default:
        return "runtime error: access on empty container";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_access(log))
      {
      case EmptyContainerAccessKind::Front:
        return "check empty() before calling front(), or ensure the container has at least one element";

      case EmptyContainerAccessKind::Back:
        return "check empty() before calling back(), or ensure the container has at least one element";

      case EmptyContainerAccessKind::PopFront:
        return "check empty() before calling pop_front(), or keep the container non-empty by invariant";

      case EmptyContainerAccessKind::PopBack:
        return "check empty() before calling pop_back(), or keep the container non-empty by invariant";

      case EmptyContainerAccessKind::Top:
        return "check empty() before calling top() on stack, queue, or priority_queue";

      case EmptyContainerAccessKind::DereferenceBeginEnd:
        return "check empty() before dereferencing begin(), or verify that the iterator is not end()";

      case EmptyContainerAccessKind::Generic:
      default:
        return "check empty() before accessing the first, last, or top element of a container";
      }
    }

    std::vector<std::string> source_patterns_for_empty_container()
    {
      return {
          ".front(",
          ".front()",
          ".back(",
          ".back()",
          ".pop_front(",
          ".pop_front()",
          ".pop_back(",
          ".pop_back()",
          ".top(",
          ".top()",
          ".begin(",
          ".begin()",
          "*",
          "std::vector",
          "std::deque",
          "std::list",
          "std::queue",
          "std::stack",
          "std::priority_queue",
          "std::string",
      };
    }

    bool looks_like_empty_container_access_log(const std::string &log)
    {
      const bool hasEmptySignal =
          icontains(log, "empty") ||
          icontains(log, "empty container") ||
          icontains(log, "container is empty") ||
          icontains(log, "sequence is empty") ||
          icontains(log, "cannot access") ||
          icontains(log, "out of bounds") ||
          icontains(log, "past-the-end") ||
          icontains(log, "past the end");

      const bool hasAccessSignal =
          icontains(log, "front") ||
          icontains(log, "back") ||
          icontains(log, "pop_front") ||
          icontains(log, "pop_back") ||
          icontains(log, "top") ||
          icontains(log, "begin") ||
          icontains(log, "end");

      return (hasEmptySignal && hasAccessSignal) ||
             icontains(log, "front() called on empty container") ||
             icontains(log, "back() called on empty container") ||
             icontains(log, "cannot call front on empty") ||
             icontains(log, "cannot call back on empty") ||
             icontains(log, "attempt to access an element in an empty container") ||
             icontains(log, "attempt to access front of empty") ||
             icontains(log, "attempt to access back of empty");
    }
  } // namespace

  class EmptyContainerFrontBackRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_empty_container_access_log(log);
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
                source_patterns_for_empty_container());
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
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 18);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeEmptyContainerFrontBackRule()
  {
    return std::make_unique<EmptyContainerFrontBackRule>();
  }
} // namespace vix::cli::errors::runtime
