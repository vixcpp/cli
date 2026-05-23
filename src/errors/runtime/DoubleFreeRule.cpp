/**
 *
 *  @file DoubleFreeRule.cpp
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
    enum class DoubleFreeKind
    {
      AsanDoubleFree,
      LibcDoubleFree,
      GenericDoubleFree,
    };

    DoubleFreeKind classify_issue(const std::string &log)
    {
      if (icontains(log, "AddressSanitizer") &&
          (icontains(log, "double-free") || icontains(log, "double free")))
      {
        return DoubleFreeKind::AsanDoubleFree;
      }

      if (icontains(log, "free(): double free") ||
          icontains(log, "double free detected"))
      {
        return DoubleFreeKind::LibcDoubleFree;
      }

      return DoubleFreeKind::GenericDoubleFree;
    }

    std::string choose_message(const std::string &log)
    {
      (void)log;
      return "double free";
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case DoubleFreeKind::AsanDoubleFree:
      case DoubleFreeKind::LibcDoubleFree:
      case DoubleFreeKind::GenericDoubleFree:
      default:
        return "the same allocation was freed twice; check duplicate ownership";
      }
    }

    std::vector<std::string> source_patterns_for_double_free()
    {
      return {
          "delete[]",
          "delete",
          "free(",
          "std::unique_ptr",
          "std::shared_ptr",
          ".reset(",
          ".clear(",
      };
    }

    bool looks_like_double_free_log(const std::string &log)
    {
      return icontains(log, "double-free") ||
             icontains(log, "double free") ||
             icontains(log, "free(): double free") ||
             icontains(log, "attempting double-free") ||
             icontains(log, "double free detected");
    }
  } // namespace

  class DoubleFreeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_double_free_log(log);
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
            source_patterns_for_double_free());
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
              "prefer RAII, std::unique_ptr, std::vector, or clear ownership rules",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeDoubleFreeRule()
  {
    return std::make_unique<DoubleFreeRule>();
  }
} // namespace vix::cli::errors::runtime
