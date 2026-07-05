/**
 *
 *  @file MisalignedAccessRule.cpp
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
      return "misaligned memory access";
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "check casts, packed structs, alignment requirements, and pointer reinterpretation";
    }

    std::vector<std::string> source_patterns_for_misaligned()
    {
      return {
          "reinterpret_cast",
          "static_cast",
          "alignas",
          "std::bit_cast",
          "std::span",
          "std::byte",
          "char*",
      };
    }

    bool looks_like_misaligned_log(const std::string &log)
    {
      return icontains(log, "misaligned address") ||
             icontains(log, "load of misaligned address") ||
             icontains(log, "store to misaligned address") ||
             icontains(log, "reference binding to misaligned address");
    }
  } // namespace

  class MisalignedAccessRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_misaligned_log(log);
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
            source_patterns_for_misaligned());
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
              "prefer std::memcpy for type punning instead of reinterpret_cast on misaligned buffers",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeMisalignedAccessRule()
  {
    return std::make_unique<MisalignedAccessRule>();
  }
} // namespace vix::cli::errors::runtime
