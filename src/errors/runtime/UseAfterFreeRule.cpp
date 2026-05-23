/**
 *
 *  @file UseAfterFreeRule.cpp
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
    enum class UseAfterFreeKind
    {
      HeapUseAfterFree,
      ContainerInvalidatedStorage,
      DanglingPointer,
      GenericUseAfterFree,
    };

    UseAfterFreeKind classify_issue(const std::string &log)
    {
      if (icontains(log, "heap-use-after-free"))
        return UseAfterFreeKind::HeapUseAfterFree;

      if (icontains(log, ".clear(") ||
          icontains(log, ".erase(") ||
          icontains(log, "container"))
      {
        return UseAfterFreeKind::ContainerInvalidatedStorage;
      }

      if (icontains(log, "dangling"))
        return UseAfterFreeKind::DanglingPointer;

      return UseAfterFreeKind::GenericUseAfterFree;
    }

    std::string choose_message(const std::string &log)
    {
      (void)log;
      return "use-after-free";
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case UseAfterFreeKind::HeapUseAfterFree:
      case UseAfterFreeKind::ContainerInvalidatedStorage:
      case UseAfterFreeKind::DanglingPointer:
      case UseAfterFreeKind::GenericUseAfterFree:
      default:
        return "you used memory after it was freed; check dangling pointers and ownership";
      }
    }

    std::vector<std::string> source_patterns_for_use_after_free()
    {
      return {
          "delete[]",
          "delete",
          "free(",
          ".reset(",
          ".clear(",
          ".erase(",
          ".data()",
          "std::string_view",
          "std::span",
      };
    }

    bool looks_like_use_after_free_log(const std::string &log)
    {
      if (icontains(log, "heap-use-after-free") ||
          icontains(log, "use-after-free"))
      {
        return true;
      }

      if (icontains(log, "AddressSanitizer") &&
          icontains(log, "use-after-free"))
      {
        return true;
      }

      return false;
    }
  } // namespace

  class UseAfterFreeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_use_after_free_log(log);
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
            source_patterns_for_use_after_free());
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
              "prefer smart pointers and avoid storing raw pointers/views into freed storage",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeUseAfterFreeRule()
  {
    return std::make_unique<UseAfterFreeRule>();
  }
} // namespace vix::cli::errors::runtime
