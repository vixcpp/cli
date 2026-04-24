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
  class EmptyContainerFrontBackRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return (icontains(log, "front") && icontains(log, "empty")) ||
             (icontains(log, "back") && icontains(log, "empty")) ||
             icontains(log, "front() called on empty container") ||
             icontains(log, "back() called on empty container") ||
             icontains(log, "cannot call front on empty") ||
             icontains(log, "cannot call back on empty") ||
             (icontains(log, "empty container") &&
              (icontains(log, "front()") || icontains(log, "back()")));
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      std::string title = "runtime error: access on empty container";
      std::vector<std::string> hints = {
          "front() or back() was used on an empty container",
          "check empty() before calling front() or back()",
      };

      if (icontains(log, "front"))
      {
        title = "runtime error: front() on empty container";
        hints = {
            "front() was called but the container has no elements",
            "check empty() first or ensure the container is filled before accessing the first element",
        };
      }
      else if (icontains(log, "back"))
      {
        title = "runtime error: back() on empty container";
        hints = {
            "back() was called but the container has no elements",
            "check empty() first or ensure the container is filled before accessing the last element",
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

  std::unique_ptr<IRuntimeErrorRule> makeEmptyContainerFrontBackRule()
  {
    return std::make_unique<EmptyContainerFrontBackRule>();
  }
} // namespace vix::cli::errors::runtime
