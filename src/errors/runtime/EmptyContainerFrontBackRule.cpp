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

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  namespace
  {
    std::string choose_title(const std::string &log)
    {
      if (icontains(log, "front"))
        return "runtime error: front() on empty container";

      if (icontains(log, "back"))
        return "runtime error: back() on empty container";

      return "runtime error: access on empty container";
    }

    std::string choose_hint(const std::string &log)
    {
      if (icontains(log, "front"))
        return "check empty() before calling front(), or ensure the container has at least one element";

      if (icontains(log, "back"))
        return "check empty() before calling back(), or ensure the container has at least one element";

      return "check empty() before accessing the first or last element of a container";
    }
  } // namespace

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
            "empty container access");

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

  std::unique_ptr<IRuntimeErrorRule> makeEmptyContainerFrontBackRule()
  {
    return std::make_unique<EmptyContainerFrontBackRule>();
  }
} // namespace vix::cli::errors::runtime
