/**
 *
 *  @file InvalidIteratorDereferenceRule.cpp
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
    enum class IteratorDereferenceKind
    {
      EndIterator,
      SingularIterator,
      PastTheEndIterator,
      DefaultConstructedIterator,
      NonDereferenceableIterator,
      InvalidatedIterator,
      NullIterator,
      GenericInvalidDereference,
    };

    IteratorDereferenceKind classify_issue(const std::string &log)
    {
      if (icontains(log, "end iterator") ||
          icontains(log, "dereference of end"))
      {
        return IteratorDereferenceKind::EndIterator;
      }

      if (icontains(log, "past-the-end") ||
          icontains(log, "past the end"))
      {
        return IteratorDereferenceKind::PastTheEndIterator;
      }

      if (icontains(log, "singular iterator") ||
          icontains(log, "singular"))
      {
        return IteratorDereferenceKind::SingularIterator;
      }

      if (icontains(log, "default-constructed") ||
          icontains(log, "default constructed"))
      {
        return IteratorDereferenceKind::DefaultConstructedIterator;
      }

      if (icontains(log, "not dereferenceable") ||
          icontains(log, "non-dereferenceable"))
      {
        return IteratorDereferenceKind::NonDereferenceableIterator;
      }

      if (icontains(log, "invalidated") ||
          icontains(log, "invalid iterator"))
      {
        return IteratorDereferenceKind::InvalidatedIterator;
      }

      if (icontains(log, "null iterator") ||
          icontains(log, "nullptr iterator"))
      {
        return IteratorDereferenceKind::NullIterator;
      }

      return IteratorDereferenceKind::GenericInvalidDereference;
    }

    std::string choose_title(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case IteratorDereferenceKind::EndIterator:
        return "runtime error: end iterator dereference";

      case IteratorDereferenceKind::SingularIterator:
        return "runtime error: singular iterator dereference";

      case IteratorDereferenceKind::PastTheEndIterator:
        return "runtime error: past-the-end iterator dereference";

      case IteratorDereferenceKind::DefaultConstructedIterator:
        return "runtime error: default-constructed iterator dereference";

      case IteratorDereferenceKind::NonDereferenceableIterator:
        return "runtime error: non-dereferenceable iterator";

      case IteratorDereferenceKind::InvalidatedIterator:
        return "runtime error: invalidated iterator dereference";

      case IteratorDereferenceKind::NullIterator:
        return "runtime error: null iterator dereference";

      case IteratorDereferenceKind::GenericInvalidDereference:
      default:
        return "runtime error: invalid iterator dereference";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case IteratorDereferenceKind::EndIterator:
        return "compare the iterator with end() before using *it or it->member";

      case IteratorDereferenceKind::SingularIterator:
        return "initialize the iterator and refresh it after erase, insert, resize, reserve, or container reallocation";

      case IteratorDereferenceKind::PastTheEndIterator:
        return "do not dereference an iterator equal to end(); check it != container.end() first";

      case IteratorDereferenceKind::DefaultConstructedIterator:
        return "assign the iterator from a real container before dereferencing it";

      case IteratorDereferenceKind::NonDereferenceableIterator:
        return "only dereference iterators that currently point to a valid container element";

      case IteratorDereferenceKind::InvalidatedIterator:
        return "refresh iterators after operations that may invalidate them, such as erase, insert, push_back, resize, or reserve";

      case IteratorDereferenceKind::NullIterator:
        return "do not dereference a null or uninitialized iterator-like object";

      case IteratorDereferenceKind::GenericInvalidDereference:
      default:
        return "check the iterator before dereferencing and avoid using invalidated iterators";
      }
    }

    std::vector<std::string> source_patterns_for_iterator_dereference()
    {
      return {
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
          "*iterator",
          "iterator->",
          "++iterator",
          "iterator++",
          "--iterator",
          "iterator--",
          ".begin(",
          ".begin()",
          ".end(",
          ".end()",
          ".find(",
          ".erase(",
          ".insert(",
          ".push_back(",
          ".reserve(",
          ".resize(",
      };
    }

    bool looks_like_invalid_iterator_dereference_log(const std::string &log)
    {
      const bool hasDereferenceSignal =
          icontains(log, "cannot dereference") ||
          icontains(log, "attempt to dereference") ||
          icontains(log, "dereference of") ||
          icontains(log, "not dereferenceable") ||
          icontains(log, "non-dereferenceable");

      const bool hasIteratorSignal =
          icontains(log, "iterator") ||
          icontains(log, "normal_iterator") ||
          icontains(log, "safe_iterator") ||
          icontains(log, "_Safe_iterator");

      const bool hasSpecificIteratorBug =
          icontains(log, "dereference of end iterator") ||
          icontains(log, "dereference of singular iterator") ||
          icontains(log, "attempt to dereference a singular iterator") ||
          icontains(log, "past-the-end iterator") ||
          icontains(log, "past the end iterator") ||
          icontains(log, "invalid iterator") ||
          icontains(log, "default-constructed iterator");

      return hasSpecificIteratorBug ||
             (hasDereferenceSignal && hasIteratorSignal) ||
             (icontains(log, "iterator") && icontains(log, "not dereferenceable"));
    }
  } // namespace

  class InvalidIteratorDereferenceRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_invalid_iterator_dereference_log(log);
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
                source_patterns_for_iterator_dereference());
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
              "do not ignore the runtime log: iterator diagnostics often explain whether the iterator is end, singular, or invalidated",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeInvalidIteratorDereferenceRule()
  {
    return std::make_unique<InvalidIteratorDereferenceRule>();
  }
} // namespace vix::cli::errors::runtime
