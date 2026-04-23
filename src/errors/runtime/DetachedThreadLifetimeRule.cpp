/**
 *
 *  @file DetachedThreadLifetimeRule.cpp
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
  class DetachedThreadLifetimeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return (icontains(log, "detach") &&
              (icontains(log, "use-after-free") ||
               icontains(log, "stack-use-after-scope") ||
               icontains(log, "use-after-return"))) ||
             (icontains(log, "detached thread") &&
              (icontains(log, "dangling") ||
               icontains(log, "invalid memory") ||
               icontains(log, "lifetime"))) ||
             (icontains(log, "lambda") &&
              icontains(log, "reference") &&
              icontains(log, "detach"));
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      std::string title = "runtime error: detached thread lifetime bug";
      std::vector<std::string> hints = {
          "a detached thread likely outlived data it was still using",
          "avoid capturing local variables by reference in detached threads and ensure shared objects outlive the thread",
      };

      if (icontains(log, "stack-use-after-scope") ||
          icontains(log, "use-after-return"))
      {
        title = "runtime error: detached thread captured expired stack data";
        hints = {
            "a detached thread likely kept a reference or pointer to a local variable after the scope ended",
            "capture by value, use shared ownership when needed, or avoid detach() for work tied to local lifetime",
        };
      }
      else if (icontains(log, "use-after-free"))
      {
        title = "runtime error: detached thread used freed memory";
        hints = {
            "a detached thread likely accessed memory after it had already been released",
            "ensure detached work owns its data safely, or join the thread before destroying shared state",
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

  std::unique_ptr<IRuntimeErrorRule> makeDetachedThreadLifetimeRule()
  {
    return std::make_unique<DetachedThreadLifetimeRule>();
  }
} // namespace vix::cli::errors::runtime
