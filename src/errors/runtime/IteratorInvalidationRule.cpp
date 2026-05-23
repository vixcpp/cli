/**
 *
 *  @file IteratorInvalidationRule.cpp
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
    enum class IteratorInvalidationKind
    {
      DereferenceInvalidated,
      IncrementInvalidated,
      CompareInvalidated,
      SingularIterator,
      VectorIteratorInvalidated,
      DequeIteratorInvalidated,
      ListIteratorInvalidated,
      AssociativeIteratorInvalidated,
      ReallocationInvalidated,
      EraseInvalidated,
      GenericInvalidation,
    };

    IteratorInvalidationKind classify_issue(const std::string &log)
    {
      if (icontains(log, "vector iterator") &&
          (icontains(log, "invalid") || icontains(log, "invalidated")))
      {
        return IteratorInvalidationKind::VectorIteratorInvalidated;
      }

      if (icontains(log, "deque iterator") &&
          (icontains(log, "invalid") || icontains(log, "invalidated")))
      {
        return IteratorInvalidationKind::DequeIteratorInvalidated;
      }

      if (icontains(log, "list iterator") &&
          (icontains(log, "invalid") || icontains(log, "invalidated")))
      {
        return IteratorInvalidationKind::ListIteratorInvalidated;
      }

      if ((icontains(log, "map iterator") ||
           icontains(log, "set iterator") ||
           icontains(log, "unordered_map iterator") ||
           icontains(log, "unordered_set iterator")) &&
          (icontains(log, "invalid") || icontains(log, "invalidated")))
      {
        return IteratorInvalidationKind::AssociativeIteratorInvalidated;
      }

      if (icontains(log, "reallocation") ||
          icontains(log, "reallocated") ||
          icontains(log, "reserve") ||
          icontains(log, "push_back") ||
          icontains(log, "resize"))
      {
        return IteratorInvalidationKind::ReallocationInvalidated;
      }

      if (icontains(log, "erase") ||
          icontains(log, "erased"))
      {
        return IteratorInvalidationKind::EraseInvalidated;
      }

      if (icontains(log, "dereference") ||
          icontains(log, "cannot dereference") ||
          icontains(log, "attempt to dereference"))
      {
        return IteratorInvalidationKind::DereferenceInvalidated;
      }

      if (icontains(log, "increment") ||
          icontains(log, "cannot increment") ||
          icontains(log, "attempt to increment"))
      {
        return IteratorInvalidationKind::IncrementInvalidated;
      }

      if (icontains(log, "compare") ||
          icontains(log, "comparison") ||
          icontains(log, "attempt to compare"))
      {
        return IteratorInvalidationKind::CompareInvalidated;
      }

      if (icontains(log, "singular iterator") ||
          icontains(log, "singular"))
      {
        return IteratorInvalidationKind::SingularIterator;
      }

      return IteratorInvalidationKind::GenericInvalidation;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case IteratorInvalidationKind::DereferenceInvalidated:
        return "invalid iterator dereference";

      case IteratorInvalidationKind::IncrementInvalidated:
        return "invalid iterator increment";

      case IteratorInvalidationKind::CompareInvalidated:
        return "invalid iterator comparison";

      case IteratorInvalidationKind::SingularIterator:
        return "singular iterator used after invalidation";

      case IteratorInvalidationKind::VectorIteratorInvalidated:
        return "vector iterator invalidated";

      case IteratorInvalidationKind::DequeIteratorInvalidated:
        return "deque iterator invalidated";

      case IteratorInvalidationKind::ListIteratorInvalidated:
        return "list iterator invalidated";

      case IteratorInvalidationKind::AssociativeIteratorInvalidated:
        return "associative container iterator invalidated";

      case IteratorInvalidationKind::ReallocationInvalidated:
        return "iterator invalidated by container reallocation";

      case IteratorInvalidationKind::EraseInvalidated:
        return "iterator invalidated by erase";

      case IteratorInvalidationKind::GenericInvalidation:
      default:
        return "iterator invalidation";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case IteratorInvalidationKind::DereferenceInvalidated:
        return "refresh the iterator before dereferencing it after container modification";

      case IteratorInvalidationKind::IncrementInvalidated:
        return "re-acquire the iterator before incrementing it after container modification";

      case IteratorInvalidationKind::CompareInvalidated:
        return "do not compare iterators that may have been invalidated by container modification";

      case IteratorInvalidationKind::SingularIterator:
        return "do not use default, moved-from, erased, or invalidated iterators";

      case IteratorInvalidationKind::VectorIteratorInvalidated:
        return "vector insert, erase, push_back, resize, reserve, and reallocation may invalidate iterators";

      case IteratorInvalidationKind::DequeIteratorInvalidated:
        return "deque insert/erase operations can invalidate iterators; re-acquire the iterator after modification";

      case IteratorInvalidationKind::ListIteratorInvalidated:
        return "list erase invalidates iterators to erased elements; use the iterator returned by erase()";

      case IteratorInvalidationKind::AssociativeIteratorInvalidated:
        return "after erase(), do not reuse the erased iterator; use the iterator returned by erase when available";

      case IteratorInvalidationKind::ReallocationInvalidated:
        return "container reallocation invalidates old iterators; reserve first or re-acquire iterators after modification";

      case IteratorInvalidationKind::EraseInvalidated:
        return "after erase(), use the returned iterator instead of the erased one";

      case IteratorInvalidationKind::GenericInvalidation:
      default:
        return "avoid reusing iterators after erase, insert, push_back, resize, reserve, or reallocation";
      }
    }

    std::vector<std::string> source_patterns_for_iterator_invalidation()
    {
      return {
          ".erase(",
          ".insert(",
          ".push_back(",
          ".emplace_back(",
          ".push_front(",
          ".emplace_front(",
          ".resize(",
          ".reserve(",
          ".clear(",
          ".remove(",
          ".remove_if(",
          "*it",
          "it->",
          "++it",
          "it++",
          "--it",
          "it--",
          "*iter",
          "iter->",
          "++iter",
          "iter++",
          "--iter",
          "iter--",
          ".begin(",
          ".end(",
      };
    }

    bool looks_like_iterator_invalidation_log(const std::string &log)
    {
      const bool explicitInvalidation =
          icontains(log, "invalidated iterator") ||
          icontains(log, "iterator invalidation") ||
          icontains(log, "cannot dereference invalidated") ||
          icontains(log, "invalid iterator");

      const bool singularUsage =
          icontains(log, "attempt to dereference a singular iterator") ||
          icontains(log, "attempt to increment a singular iterator") ||
          icontains(log, "attempt to compare a singular iterator");

      const bool containerIteratorInvalid =
          (icontains(log, "vector iterator") && icontains(log, "invalid")) ||
          (icontains(log, "deque iterator") && icontains(log, "invalid")) ||
          (icontains(log, "list iterator") && icontains(log, "invalid")) ||
          (icontains(log, "map iterator") && icontains(log, "invalid")) ||
          (icontains(log, "set iterator") && icontains(log, "invalid")) ||
          (icontains(log, "unordered_map iterator") && icontains(log, "invalid")) ||
          (icontains(log, "unordered_set iterator") && icontains(log, "invalid"));

      const bool invalidOperation =
          icontains(log, "iterator") &&
          (icontains(log, "dereference") ||
           icontains(log, "increment") ||
           icontains(log, "compare") ||
           icontains(log, "comparison")) &&
          (icontains(log, "invalid") ||
           icontains(log, "invalidated") ||
           icontains(log, "singular"));

      return explicitInvalidation ||
             singularUsage ||
             containerIteratorInvalid ||
             invalidOperation;
    }
  } // namespace

  class IteratorInvalidationRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_iterator_invalidation_log(log);
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
                source_patterns_for_iterator_invalidation());
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
              "do not ignore the runtime log: STL debug mode often explains which operation invalidated the iterator",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeIteratorInvalidationRule()
  {
    return std::make_unique<IteratorInvalidationRule>();
  }
} // namespace vix::cli::errors::runtime
