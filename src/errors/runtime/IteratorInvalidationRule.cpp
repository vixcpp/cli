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
  class IteratorInvalidationRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return icontains(log, "invalidated iterator") ||
             icontains(log, "iterator invalidation") ||
             icontains(log, "cannot dereference invalidated") ||
             icontains(log, "attempt to dereference a singular iterator") ||
             icontains(log, "attempt to increment a singular iterator") ||
             icontains(log, "attempt to compare a singular iterator") ||
             (icontains(log, "vector iterator") && icontains(log, "invalid")) ||
             (icontains(log, "deque iterator") && icontains(log, "invalid")) ||
             (icontains(log, "list iterator") && icontains(log, "invalid"));
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      std::string title = "runtime error: iterator invalidation";
      std::vector<std::string> hints = {
          "an iterator was used after the container changed its valid iterator range",
          "check insert(), erase(), push_back(), resize(), reserve(), and reallocation effects before reusing iterators",
      };

      if (icontains(log, "dereference"))
      {
        title = "runtime error: invalid iterator dereference";
        hints = {
            "an invalidated iterator was dereferenced",
            "refresh the iterator after container modification and avoid keeping old iterators across erase or reallocation",
        };
      }
      else if (icontains(log, "increment"))
      {
        title = "runtime error: invalid iterator increment";
        hints = {
            "an invalidated iterator was incremented",
            "re-acquire the iterator after container mutation before continuing iteration",
        };
      }
      else if (icontains(log, "compare"))
      {
        title = "runtime error: invalid iterator comparison";
        hints = {
            "an invalidated iterator was compared after container mutation",
            "do not compare iterators that may have been invalidated by erase, insert, or reallocation",
        };
      }

      std::cerr << RED
                << title
                << RESET << "\n";

      print_runtime_hints_and_at(
          hints,
          !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeIteratorInvalidationRule()
  {
    return std::make_unique<IteratorInvalidationRule>();
  }
} // namespace vix::cli::errors::runtime
