/**
 *
 *  @file IntegerOverflowRule.cpp
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
    enum class IntegerOverflowKind
    {
      SignedIntegerOverflow,
      UnsignedIntegerOverflow,
      InvalidShift,
      UnsignedOffset,
      GenericOverflow,
    };

    IntegerOverflowKind classify_issue(const std::string &log)
    {
      if (icontains(log, "signed integer overflow"))
        return IntegerOverflowKind::SignedIntegerOverflow;

      if (icontains(log, "unsigned integer overflow"))
        return IntegerOverflowKind::UnsignedIntegerOverflow;

      if (icontains(log, "shift exponent") ||
          icontains(log, "shift out of bounds"))
      {
        return IntegerOverflowKind::InvalidShift;
      }

      if (icontains(log, "addition of unsigned offset"))
        return IntegerOverflowKind::UnsignedOffset;

      return IntegerOverflowKind::GenericOverflow;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case IntegerOverflowKind::SignedIntegerOverflow:
        return "signed integer overflow";

      case IntegerOverflowKind::InvalidShift:
        return "invalid shift";

      case IntegerOverflowKind::UnsignedIntegerOverflow:
      case IntegerOverflowKind::UnsignedOffset:
      case IntegerOverflowKind::GenericOverflow:
      default:
        return "integer overflow";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "use wider integer types or check bounds before arithmetic";
    }

    std::vector<std::string> source_patterns_for_integer_overflow()
    {
      return {
          "+=",
          "-=",
          "*=",
          "<<",
          ">>",
          "std::int64_t",
          "std::size_t",
          "+",
          "-",
          "*",
      };
    }

    bool looks_like_integer_overflow_log(const std::string &log)
    {
      if (icontains(log, "signed integer overflow") ||
          icontains(log, "unsigned integer overflow") ||
          icontains(log, "integer overflow"))
      {
        return true;
      }

      if (icontains(log, "addition of unsigned offset") ||
          icontains(log, "shift exponent") ||
          icontains(log, "shift out of bounds"))
      {
        return true;
      }

      return false;
    }
  } // namespace

  class IntegerOverflowRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_integer_overflow_log(log);
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
            source_patterns_for_integer_overflow());
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
              "signed overflow is undefined behavior; validate inputs from untrusted sources",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeIntegerOverflowRule()
  {
    return std::make_unique<IntegerOverflowRule>();
  }
} // namespace vix::cli::errors::runtime
