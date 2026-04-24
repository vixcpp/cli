/**
 *
 *  @file StringViewDanglingRuntimeRule.cpp
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
  class StringViewDanglingRuntimeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return (icontains(log, "string_view") &&
              (icontains(log, "dangling") ||
               icontains(log, "lifetime") ||
               icontains(log, "invalid"))) ||
             (icontains(log, "basic_string_view") &&
              (icontains(log, "dangling") ||
               icontains(log, "invalid"))) ||
             (icontains(log, "stack-use-after-scope") && icontains(log, "string_view")) ||
             (icontains(log, "use-after-return") && icontains(log, "string_view")) ||
             (icontains(log, "heap-use-after-free") && icontains(log, "string_view"));
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      std::string title = "runtime error: dangling std::string_view";
      std::vector<std::string> hints = {
          "a std::string_view likely points to characters that no longer exist",
          "do not keep string_view to a temporary string, a destroyed local string, or reallocated string storage",
      };

      if (icontains(log, "stack-use-after-scope") || icontains(log, "use-after-return"))
      {
        title = "runtime error: std::string_view outlived local string data";
        hints = {
            "a std::string_view likely refers to characters owned by a local object that already went out of scope",
            "return std::string when ownership is needed, or ensure the viewed storage outlives the string_view",
        };
      }
      else if (icontains(log, "heap-use-after-free"))
      {
        title = "runtime error: std::string_view points to freed memory";
        hints = {
            "a std::string_view likely refers to string storage that has already been released",
            "avoid keeping string_view after the owning string is destroyed, moved, cleared, or reallocated",
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

  std::unique_ptr<IRuntimeErrorRule> makeStringViewDanglingRuntimeRule()
  {
    return std::make_unique<StringViewDanglingRuntimeRule>();
  }
} // namespace vix::cli::errors::runtime
