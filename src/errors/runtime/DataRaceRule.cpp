/**
 *
 *  @file DataRaceRule.cpp
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
  class DataRaceRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return icontains(log, "ThreadSanitizer") ||
             icontains(log, "data race");
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)log;

      std::cerr << RED
                << "runtime error: data race"
                << RESET << "\n";

      print_runtime_hints_and_at(
          {
              "two or more threads accessed the same memory without proper synchronization",
              "protect shared state with std::mutex, std::scoped_lock, or std::atomic",
          },
          !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeDataRaceRule()
  {
    return std::make_unique<DataRaceRule>();
  }
} // namespace vix::cli::errors::runtime
