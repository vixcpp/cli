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

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  namespace
  {
    std::string choose_title(const std::string &log)
    {
      if (icontains(log, "end iterator"))
        return "runtime error: end iterator dereference";

      if (icontains(log, "singular iterator"))
        return "runtime error: singular iterator dereference";

      if (icontains(log, "not dereferenceable"))
        return "runtime error: non-dereferenceable iterator";

      return "runtime error: invalid iterator dereference";
    }

    std::string choose_hint(const std::string &log)
    {
      if (icontains(log, "end iterator"))
        return "compare the iterator with end() before using *it or it->member";

      if (icontains(log, "singular iterator"))
        return "initialize the iterator and refresh it after erase, insert, or container reallocation";

      if (icontains(log, "not dereferenceable"))
        return "only dereference iterators that currently point to a valid container element";

      return "check the iterator before dereferencing and avoid using invalidated iterators";
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

      return icontains(log, "cannot dereference") ||
             icontains(log, "attempt to dereference") ||
             icontains(log, "dereference of end iterator") ||
             icontains(log, "dereference of singular iterator") ||
             icontains(log, "attempt to dereference a singular iterator") ||
             (icontains(log, "invalid iterator") && icontains(log, "dereference")) ||
             (icontains(log, "iterator") && icontains(log, "not dereferenceable"));
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const RuntimeLocation location =
          find_best_runtime_location_or_source_hint(
              log,
              sourceFile,
              {
                  "*it",
                  "it->",
                  "++it",
                  "it++",
                  "*iter",
                  "iter->",
                  "++iter",
                  "iter++",
                  "*iterator",
                  "iterator->",
                  "++iterator",
                  "iterator++",
              });

      std::cerr << RED
                << choose_title(log)
                << RESET << "\n";

      if (location.valid())
      {
        const auto err = make_runtime_location(
            location.file,
            location.line,
            location.column,
            "invalid iterator dereference");

        print_runtime_codeframe(err);
      }

      print_runtime_hints_and_at(
          {
              choose_hint(log),
          },
          make_at_text(location, sourceFile));

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeInvalidIteratorDereferenceRule()
  {
    return std::make_unique<InvalidIteratorDereferenceRule>();
  }
} // namespace vix::cli::errors::runtime
