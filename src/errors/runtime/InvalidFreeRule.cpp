/**
 *
 *  @file InvalidFreeRule.cpp
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
    enum class InvalidFreeKind
    {
      AsanBadFree,
      LibcInvalidPointer,
      LibcMunmapInvalid,
      GenericInvalidFree,
    };

    InvalidFreeKind classify_issue(const std::string &log)
    {
      if (icontains(log, "AddressSanitizer") &&
          (icontains(log, "bad-free") ||
           icontains(log, "attempting free") ||
           icontains(log, "not malloc()-ed")))
      {
        return InvalidFreeKind::AsanBadFree;
      }

      if (icontains(log, "munmap_chunk(): invalid pointer"))
        return InvalidFreeKind::LibcMunmapInvalid;

      if (icontains(log, "free(): invalid pointer"))
        return InvalidFreeKind::LibcInvalidPointer;

      return InvalidFreeKind::GenericInvalidFree;
    }

    std::string choose_message(const std::string &log)
    {
      (void)log;
      return "invalid free";
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case InvalidFreeKind::AsanBadFree:
      case InvalidFreeKind::LibcInvalidPointer:
      case InvalidFreeKind::LibcMunmapInvalid:
      case InvalidFreeKind::GenericInvalidFree:
      default:
        return "only free memory allocated by the matching allocation API";
      }
    }

    std::vector<std::string> source_patterns_for_invalid_free()
    {
      return {
          "delete[]",
          "delete",
          "free(",
          "malloc(",
          "new[]",
          "new ",
          "std::vector",
          "std::string",
      };
    }

    bool looks_like_invalid_free_log(const std::string &log)
    {
      if (icontains(log, "free(): invalid pointer") ||
          icontains(log, "munmap_chunk(): invalid pointer") ||
          icontains(log, "bad-free"))
      {
        return true;
      }

      if (icontains(log, "AddressSanitizer") &&
          (icontains(log, "attempting free") ||
           icontains(log, "not malloc()-ed")))
      {
        return true;
      }

      // "invalid pointer" alone is too generic without one of the above signals
      return false;
    }
  } // namespace

  class InvalidFreeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_invalid_free_log(log);
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
            source_patterns_for_invalid_free());
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
              "do not free stack memory, string literals, or memory owned by containers",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeInvalidFreeRule()
  {
    return std::make_unique<InvalidFreeRule>();
  }
} // namespace vix::cli::errors::runtime
