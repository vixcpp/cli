/**
 *
 *  @file NullPointerRule.cpp
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
    enum class NullPointerKind
    {
      UbsanMemberAccessNull,
      UbsanLoadNull,
      UbsanStoreNull,
      SegvAtZero,
      GenericNullDeref,
    };

    NullPointerKind classify_issue(const std::string &log)
    {
      if (icontains(log, "member access within null pointer"))
        return NullPointerKind::UbsanMemberAccessNull;

      if (icontains(log, "load of null pointer"))
        return NullPointerKind::UbsanLoadNull;

      if (icontains(log, "store to null pointer"))
        return NullPointerKind::UbsanStoreNull;

      if (icontains(log, "SEGV_MAPERR") && icontains(log, "0x0"))
        return NullPointerKind::SegvAtZero;

      if (icontains(log, "address 0x0"))
        return NullPointerKind::SegvAtZero;

      return NullPointerKind::GenericNullDeref;
    }

    std::string choose_message(const std::string &log)
    {
      (void)log;
      return "null pointer dereference";
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "check pointers before dereferencing them";
    }

    std::vector<std::string> source_patterns_for_null_pointer()
    {
      return {
          "= nullptr",
          "nullptr",
          ".get()",
          "std::unique_ptr",
          "std::shared_ptr",
          "->",
          "*",
      };
    }

    bool looks_like_null_pointer_log(const std::string &log)
    {
      if (icontains(log, "null pointer") ||
          icontains(log, "member access within null pointer") ||
          icontains(log, "load of null pointer") ||
          icontains(log, "store to null pointer"))
      {
        return true;
      }

      if (icontains(log, "SEGV_MAPERR") && icontains(log, "0x0"))
        return true;

      if (icontains(log, "address 0x0") &&
          (icontains(log, "SIGSEGV") || icontains(log, "segmentation")))
      {
        return true;
      }

      return false;
    }
  } // namespace

  class NullPointerRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_null_pointer_log(log);
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
            source_patterns_for_null_pointer());
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
              "prefer references, std::optional, or smart pointers with explicit null checks",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeNullPointerRule()
  {
    return std::make_unique<NullPointerRule>();
  }
} // namespace vix::cli::errors::runtime
