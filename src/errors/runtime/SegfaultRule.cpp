/**
 *
 *  @file SegfaultRule.cpp
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
  class SegfaultRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return icontains(log, "segmentation fault") ||
             icontains(log, "sigsegv") ||
             icontains(log, "signal 11");
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)log;

      std::cerr << RED
                << "runtime error: segmentation fault"
                << RESET << "\n";

      print_runtime_hints_and_at(
          {
              "the program accessed invalid memory",
              "check null pointers, dangling pointers, out-of-bounds access, and invalid references",
          },
          !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeSegfaultRule()
  {
    return std::make_unique<SegfaultRule>();
  }
} // namespace vix::cli::errors::runtime
