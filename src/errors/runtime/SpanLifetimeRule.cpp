/**
 *
 *  @file SpanLifetimeRule.cpp
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
  class SpanLifetimeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return (icontains(log, "span") &&
              (icontains(log, "dangling") ||
               icontains(log, "lifetime") ||
               icontains(log, "invalid"))) ||
             (icontains(log, "std::span") &&
              (icontains(log, "dangling") ||
               icontains(log, "invalid"))) ||
             (icontains(log, "stack-use-after-scope") &&
              (icontains(log, "span") || icontains(log, "std::span"))) ||
             (icontains(log, "use-after-return") &&
              (icontains(log, "span") || icontains(log, "std::span"))) ||
             (icontains(log, "heap-use-after-free") &&
              (icontains(log, "span") || icontains(log, "std::span")));
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      std::string title = "runtime error: dangling std::span";
      std::vector<std::string> hints = {
          "a std::span likely refers to elements that no longer exist",
          "do not keep span after the underlying array, vector, or buffer changes lifetime or storage",
      };

      if (icontains(log, "stack-use-after-scope") || icontains(log, "use-after-return"))
      {
        title = "runtime error: std::span outlived local storage";
        hints = {
            "a std::span likely refers to local data that already went out of scope",
            "ensure the underlying storage outlives the span, or return an owning container when ownership is needed",
        };
      }
      else if (icontains(log, "heap-use-after-free"))
      {
        title = "runtime error: std::span points to freed memory";
        hints = {
            "a std::span likely refers to storage that has already been released",
            "avoid keeping span after the owning container is destroyed, cleared, resized, or reallocated",
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

  std::unique_ptr<IRuntimeErrorRule> makeSpanLifetimeRule()
  {
    return std::make_unique<SpanLifetimeRule>();
  }
} // namespace vix::cli::errors::runtime
