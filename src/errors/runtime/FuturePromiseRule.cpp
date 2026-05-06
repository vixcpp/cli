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

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  namespace
  {
    std::string choose_title(const std::string &log)
    {
      if (icontains(log, "broken promise"))
        return "runtime error: broken promise";

      if (icontains(log, "promise already satisfied"))
        return "runtime error: promise already satisfied";

      if (icontains(log, "future already retrieved"))
        return "runtime error: future already retrieved";

      if (icontains(log, "no associated state"))
        return "runtime error: invalid future state";

      return "runtime error: future/promise misuse";
    }

    std::string choose_hint(const std::string &log)
    {
      if (icontains(log, "broken promise"))
        return "set a value or exception before the promise is destroyed";

      if (icontains(log, "promise already satisfied"))
        return "call set_value() or set_exception() only once for the same promise";

      if (icontains(log, "future already retrieved"))
        return "call get_future() only once for the same promise";

      if (icontains(log, "no associated state"))
        return "avoid using moved-from futures or promises without a valid shared state";

      return "check future/promise ownership and ensure the shared state is consumed or fulfilled only once";
    }
  } // namespace

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
            "future/promise misuse");

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

  std::unique_ptr<IRuntimeErrorRule> makeFuturePromiseRule()
  {
    return std::make_unique<FuturePromiseRule>();
  }
} // namespace vix::cli::errors::runtime
