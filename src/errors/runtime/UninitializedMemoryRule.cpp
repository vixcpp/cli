/**
 *
 *  @file UninitializedMemoryRule.cpp
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
    enum class UninitializedMemoryKind
    {
      MsanUninitValue,
      ValgrindConditionalJump,
      CompilerUninitialized,
      GenericUninit,
    };

    UninitializedMemoryKind classify_issue(const std::string &log)
    {
      if (icontains(log, "MemorySanitizer") ||
          icontains(log, "use-of-uninitialized-value"))
      {
        return UninitializedMemoryKind::MsanUninitValue;
      }

      if (icontains(log, "conditional jump or move depends on uninitialised value"))
        return UninitializedMemoryKind::ValgrindConditionalJump;

      if (icontains(log, "is used uninitialized"))
        return UninitializedMemoryKind::CompilerUninitialized;

      return UninitializedMemoryKind::GenericUninit;
    }

    std::string choose_message(const std::string &log)
    {
      (void)log;
      return "uninitialized memory read";
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "initialize variables and buffers before reading them";
    }

    std::vector<std::string> source_patterns_for_uninitialized()
    {
      return {
          "std::array",
          "malloc(",
          "new ",
          "int ",
          "double ",
          "bool ",
          "char ",
      };
    }

    bool looks_like_uninitialized_log(const std::string &log)
    {
      return icontains(log, "MemorySanitizer") ||
             icontains(log, "use-of-uninitialized-value") ||
             icontains(log, "uninitialized value") ||
             icontains(log, "conditional jump or move depends on uninitialised value") ||
             icontains(log, "is used uninitialized");
    }
  } // namespace

  class UninitializedMemoryRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_uninitialized_log(log);
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
            source_patterns_for_uninitialized());
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
              "prefer default member initializers and value-initialized objects like T{}",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeUninitializedMemoryRule()
  {
    return std::make_unique<UninitializedMemoryRule>();
  }
} // namespace vix::cli::errors::runtime
