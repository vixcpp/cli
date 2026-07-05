/**
 *
 *  @file UncaughtExceptionRuntimeRule.cpp
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
      return "uncaught exception";
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "catch exceptions at the runtime boundary and handle or report e.what()";
    }

    std::vector<std::string> source_patterns_for_uncaught()
    {
      return {
          "throw ",
          "std::runtime_error",
          "std::logic_error",
          "std::invalid_argument",
          "std::out_of_range",
          "try",
          "catch",
      };
    }

    bool looks_like_uncaught_exception_log(const std::string &log)
    {
      if (icontains(log, "terminate called after throwing an instance of"))
        return true;

      if (icontains(log, "uncaught exception"))
        return true;

      // Require "what():" together with terminate or std::terminate context.
      if (icontains(log, "what():") &&
          (icontains(log, "terminate called") ||
           icontains(log, "std::terminate") ||
           icontains(log, "Aborted")))
      {
        return true;
      }

      return false;
    }
  } // namespace

  class UncaughtExceptionRuntimeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_uncaught_exception_log(log);
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
            source_patterns_for_uncaught());
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
              "the runtime log below shows the exception type and its what() message",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeUncaughtExceptionRuntimeRule()
  {
    return std::make_unique<UncaughtExceptionRuntimeRule>();
  }
} // namespace vix::cli::errors::runtime
