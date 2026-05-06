/**
 *
 *  @file DeadlockRule.cpp
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
  class DeadlockRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return icontains(log, "deadlock") ||
             icontains(log, "resource deadlock avoided") ||
             icontains(log, "e deadlk") ||
             icontains(log, "std::system_error: resource deadlock avoided") ||
             icontains(log, "ThreadSanitizer: lock-order-inversion");
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const RuntimeLocation location =
          find_best_runtime_location(log, sourceFile);

      std::cerr << RED
                << "runtime error: deadlock"
                << RESET << "\n";

      if (location.valid())
      {
        const auto err = make_runtime_location(
            location.file,
            location.line,
            location.column,
            "deadlock");

        print_runtime_codeframe(err);
      }

      print_runtime_hints_and_at(
          {
              "lock shared resources in a consistent order and prefer std::scoped_lock when acquiring multiple mutexes",
          },
          make_at_text(location, sourceFile));

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeDeadlockRule()
  {
    return std::make_unique<DeadlockRule>();
  }
} // namespace vix::cli::errors::runtime
