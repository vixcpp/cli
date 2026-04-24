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
      std::string title = "runtime error: invalid iterator dereference";
      std::vector<std::string> hints = {
          "an iterator was dereferenced when it did not point to a valid element",
          "check for end() before dereferencing and avoid using invalidated or singular iterators",
      };

      if (icontains(log, "end iterator"))
      {
        title = "runtime error: end iterator dereference";
        hints = {
            "end() was dereferenced, but it points past the last element",
            "compare against end() before using *it or it->member",
        };
      }
      else if (icontains(log, "singular iterator"))
      {
        title = "runtime error: singular iterator dereference";
        hints = {
            "a default-constructed, erased, or otherwise invalid iterator was dereferenced",
            "initialize iterators properly and refresh them after erase, insert, or container reallocation",
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

  std::unique_ptr<IRuntimeErrorRule> makeInvalidIteratorDereferenceRule()
  {
    return std::make_unique<InvalidIteratorDereferenceRule>();
  }
} // namespace vix::cli::errors::runtime
