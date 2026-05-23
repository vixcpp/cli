/**
 *
 *  @file DivisionByZeroRule.cpp
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
    enum class DivisionByZeroKind
    {
      IntegerDivisionByZero,
      FloatingPointException,
      Sigfpe,
      GenericDivisionByZero,
    };

    DivisionByZeroKind classify_issue(const std::string &log)
    {
      if (icontains(log, "integer division by zero"))
        return DivisionByZeroKind::IntegerDivisionByZero;

      if (icontains(log, "floating point exception"))
        return DivisionByZeroKind::FloatingPointException;

      if (icontains(log, "SIGFPE"))
        return DivisionByZeroKind::Sigfpe;

      return DivisionByZeroKind::GenericDivisionByZero;
    }

    std::string choose_message(const std::string &log)
    {
      (void)log;
      return "division by zero";
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "guard denominators before division or modulo";
    }

    std::vector<std::string> source_patterns_for_division_by_zero()
    {
      return {
          "/=",
          "%=",
          "/",
          "%",
      };
    }

    bool looks_like_division_by_zero_log(const std::string &log)
    {
      if (icontains(log, "division by zero") ||
          icontains(log, "divide by zero") ||
          icontains(log, "integer division by zero"))
      {
        return true;
      }

      if (icontains(log, "floating point exception") ||
          icontains(log, "SIGFPE"))
      {
        return true;
      }

      return false;
    }
  } // namespace

  class DivisionByZeroRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_division_by_zero_log(log);
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
            source_patterns_for_division_by_zero());
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
              "for integer division, a zero denominator is undefined behavior; check the input",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeDivisionByZeroRule()
  {
    return std::make_unique<DivisionByZeroRule>();
  }
} // namespace vix::cli::errors::runtime
