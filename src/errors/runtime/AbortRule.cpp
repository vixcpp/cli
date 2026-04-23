/**
 *
 *  @file AbortRule.cpp
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
  class AbortRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return icontains(log, "aborted") ||
             icontains(log, "sigabrt") ||
             icontains(log, "signal 6") ||
             icontains(log, "abort()") ||
             icontains(log, "std::abort") ||
             icontains(log, "core dumped") ||
             icontains(log, "terminate called");
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)log;

      std::cerr << RED
                << "runtime error: aborted"
                << RESET << "\n";

      print_runtime_hints_and_at(
          {
              "the program aborted itself or was terminated after a fatal runtime failure",
              "check assertions, std::terminate(), uncaught exceptions, and invalid runtime states",
          },
          !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeAbortRule()
  {
    return std::make_unique<AbortRule>();
  }
} // namespace vix::cli::errors::runtime
