/**
 *
 *  @file BadVariantAccessRule.cpp
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
    bool looks_like_bad_variant_access_log(const std::string &log)
    {
      return icontains(log, "bad_variant_access") ||
             icontains(log, "std::get: wrong index for variant");
    }

    std::vector<std::string> source_patterns_for_bad_variant_access()
    {
      return {
          ".value()",
          "value()",
          "std::get<",
          "std::get(",
      };
    }
  } // namespace

  class BadVariantAccessRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_bad_variant_access_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const std::string message =
          "accessed a value that is not present";

      RuntimeLocation location = find_best_runtime_location(log, sourceFile);

      if (!location.valid())
      {
        location = find_best_runtime_location_or_source_hint(
            log,
            sourceFile,
            source_patterns_for_bad_variant_access());
      }

      std::cerr << RED
                << "runtime error: "
                << message
                << RESET << "\n";

      if (location.valid())
      {
        const auto err = make_runtime_location(
            location.file,
            location.line,
            location.column,
            message);

        print_runtime_codeframe(err);
      }

      print_runtime_hints_and_at(
          {
              "check the result before calling .value()",
              "use `if (!result)` and inspect `result.error()` before reading the value",
          },
          make_at_text(location, sourceFile));

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeBadVariantAccessRule()
  {
    return std::make_unique<BadVariantAccessRule>();
  }
} // namespace vix::cli::errors::runtime
