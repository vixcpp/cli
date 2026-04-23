/**
 *
 *  @file FuturePromiseRule.cpp
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
  class FuturePromiseRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return icontains(log, "std::future_error") ||
             icontains(log, "future_error") ||
             icontains(log, "broken promise") ||
             icontains(log, "promise already satisfied") ||
             icontains(log, "future already retrieved") ||
             icontains(log, "no associated state");
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      std::string title = "runtime error: future/promise misuse";
      std::vector<std::string> hints = {
          "a std::future or std::promise was used incorrectly",
          "check ownership, state validity, and whether the shared state was consumed or fulfilled more than once",
      };

      if (icontains(log, "broken promise"))
      {
        title = "runtime error: broken promise";
        hints = {
            "the promise was destroyed before setting a value or exception",
            "ensure every promise sets a value or exception before it goes out of scope",
        };
      }
      else if (icontains(log, "promise already satisfied"))
      {
        title = "runtime error: promise already satisfied";
        hints = {
            "the same promise was fulfilled more than once",
            "call set_value() or set_exception() only once for a given promise",
        };
      }
      else if (icontains(log, "future already retrieved"))
      {
        title = "runtime error: future already retrieved";
        hints = {
            "get_future() was likely called more than once on the same promise",
            "retrieve the future only once, or redesign ownership of the shared state",
        };
      }
      else if (icontains(log, "no associated state"))
      {
        title = "runtime error: invalid future state";
        hints = {
            "the future or promise has no valid shared state",
            "check moved-from objects and avoid calling get(), wait(), or set_value() on invalid state",
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

  std::unique_ptr<IRuntimeErrorRule> makeFuturePromiseRule()
  {
    return std::make_unique<FuturePromiseRule>();
  }
} // namespace vix::cli::errors::runtime
