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

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  namespace
  {
    std::string choose_title(const std::string &log)
    {
      if (icontains(log, "stack-use-after-scope") ||
          icontains(log, "use-after-return"))
      {
        return "runtime error: detached thread captured expired stack data";
      }

      if (icontains(log, "use-after-free"))
      {
        return "runtime error: detached thread used freed memory";
      }

      return "runtime error: detached thread lifetime bug";
    }

    std::string choose_hint(const std::string &log)
    {
      if (icontains(log, "stack-use-after-scope") ||
          icontains(log, "use-after-return"))
      {
        return "capture data by value, use shared ownership, or join the thread before local state goes out of scope";
      }

      if (icontains(log, "use-after-free"))
      {
        return "ensure detached work owns its data safely, or join the thread before destroying shared state";
      }

      return "avoid capturing local state by reference in detached threads unless its lifetime is guaranteed";
    }
  } // namespace

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
      const RuntimeLocation location =
          find_best_runtime_location(log, sourceFile);

      std::cerr << RED
                << choose_title(log)
                << RESET << "\n";

      if (location.valid())
      {
        const auto err = make_runtime_location(
            location.file,
            location.line,
            location.column,
            "detached thread lifetime bug");

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

  std::unique_ptr<IRuntimeErrorRule> makeDetachedThreadLifetimeRule()
  {
    return std::make_unique<DetachedThreadLifetimeRule>();
  }
} // namespace vix::cli::errors::runtime
