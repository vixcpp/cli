/**
 *
 *  @file StackOverflowRule.cpp
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
    std::string choose_message(const std::string &log)
    {
      (void)log;
      return "stack overflow";
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "reduce recursion depth or move large local objects to the heap";
    }

    std::vector<std::string> source_patterns_for_stack_overflow()
    {
      return {
          "alloca(",
          "std::array",
          "return ",
          "[",
      };
    }

    bool looks_like_stack_overflow_log(const std::string &log)
    {
      if (icontains(log, "stack-overflow") ||
          icontains(log, "stack overflow"))
      {
        return true;
      }

      if (icontains(log, "AddressSanitizer: stack-overflow"))
        return true;

      return false;
    }
  } // namespace

  class StackOverflowRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_stack_overflow_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const std::string message = choose_message(log);

      RuntimeLocation location = find_best_runtime_location(log, sourceFile);

      if (!location.valid())
      {
        location = find_best_runtime_location_or_source_hint(
            log,
            sourceFile,
            source_patterns_for_stack_overflow());
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
              choose_hint(log),
              "if recursion is required, check the base case and consider an iterative version",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeStackOverflowRule()
  {
    return std::make_unique<StackOverflowRule>();
  }
} // namespace vix::cli::errors::runtime
