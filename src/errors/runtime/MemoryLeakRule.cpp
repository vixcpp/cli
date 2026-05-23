/**
 *
 *  @file MemoryLeakRule.cpp
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
    enum class MemoryLeakKind
    {
      LeakSanitizerDirect,
      LeakSanitizerIndirect,
      ValgrindDefinitelyLost,
      ValgrindPossiblyLost,
      GenericLeak,
    };

    MemoryLeakKind classify_issue(const std::string &log)
    {
      if (icontains(log, "direct leak"))
        return MemoryLeakKind::LeakSanitizerDirect;

      if (icontains(log, "indirect leak"))
        return MemoryLeakKind::LeakSanitizerIndirect;

      if (icontains(log, "definitely lost"))
        return MemoryLeakKind::ValgrindDefinitelyLost;

      if (icontains(log, "possibly lost"))
        return MemoryLeakKind::ValgrindPossiblyLost;

      return MemoryLeakKind::GenericLeak;
    }

    std::string choose_message(const std::string &log)
    {
      (void)log;
      return "memory leak";
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case MemoryLeakKind::LeakSanitizerDirect:
      case MemoryLeakKind::LeakSanitizerIndirect:
      case MemoryLeakKind::ValgrindDefinitelyLost:
      case MemoryLeakKind::ValgrindPossiblyLost:
      case MemoryLeakKind::GenericLeak:
      default:
        return "free allocations or use RAII and smart pointers";
      }
    }

    std::vector<std::string> source_patterns_for_memory_leak()
    {
      return {
          "new[]",
          "new ",
          "malloc(",
          "calloc(",
          "realloc(",
          "std::make_unique",
          "std::make_shared",
      };
    }

    bool looks_like_memory_leak_log(const std::string &log)
    {
      return icontains(log, "LeakSanitizer") ||
             icontains(log, "detected memory leaks") ||
             icontains(log, "direct leak") ||
             icontains(log, "indirect leak") ||
             icontains(log, "definitely lost") ||
             icontains(log, "possibly lost");
    }
  } // namespace

  class MemoryLeakRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_memory_leak_log(log);
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
            source_patterns_for_memory_leak());
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
              "prefer std::unique_ptr, std::vector, std::string, and automatic storage",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeMemoryLeakRule()
  {
    return std::make_unique<MemoryLeakRule>();
  }
} // namespace vix::cli::errors::runtime
