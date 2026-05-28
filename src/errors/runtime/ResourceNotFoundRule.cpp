/**
 *
 *  @file ResourceNotFoundRule.cpp
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
  namespace
  {
    bool looks_like_resource_not_found_log(const std::string &log)
    {
      return icontains(log, "asset file not found") ||
             icontains(log, "resource not found") ||
             icontains(log, "file not found") ||
             icontains(log, "no such file or directory");
    }

    bool looks_like_load_failure(const std::string &log)
    {
      return icontains(log, "load failed") ||
             icontains(log, "failed to load") ||
             icontains(log, "open failed") ||
             icontains(log, "failed to open");
    }
  } // namespace

  class ResourceNotFoundRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return looks_like_resource_not_found_log(log) ||
             (looks_like_load_failure(log) && icontains(log, "not found"));
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      std::cerr << RED
                << "runtime error: required resource not found"
                << RESET << "\n";

      print_runtime_hints_and_at(
          {
              "check the path used to load the resource",
              "ensure the file exists relative to the working directory or configured asset root",
          },
          !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeResourceNotFoundRule()
  {
    return std::make_unique<ResourceNotFoundRule>();
  }
} // namespace vix::cli::errors::runtime
