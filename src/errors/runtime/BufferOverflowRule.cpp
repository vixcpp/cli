/**
 *
 *  @file BufferOverflowRule.cpp
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
    enum class BufferOverflowKind
    {
      HeapBufferOverflow,
      StackBufferOverflow,
      GlobalBufferOverflow,
      UbsanOutOfBounds,
      GenericOutOfBounds,
    };

    BufferOverflowKind classify_issue(const std::string &log)
    {
      if (icontains(log, "heap-buffer-overflow"))
        return BufferOverflowKind::HeapBufferOverflow;

      if (icontains(log, "stack-buffer-overflow"))
        return BufferOverflowKind::StackBufferOverflow;

      if (icontains(log, "global-buffer-overflow"))
        return BufferOverflowKind::GlobalBufferOverflow;

      if (icontains(log, "index out of bounds") ||
          icontains(log, "out of bounds index"))
      {
        return BufferOverflowKind::UbsanOutOfBounds;
      }

      return BufferOverflowKind::GenericOutOfBounds;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case BufferOverflowKind::HeapBufferOverflow:
        return "heap-buffer-overflow";

      case BufferOverflowKind::StackBufferOverflow:
        return "stack-buffer-overflow";

      case BufferOverflowKind::GlobalBufferOverflow:
        return "global-buffer-overflow";

      case BufferOverflowKind::UbsanOutOfBounds:
      case BufferOverflowKind::GenericOutOfBounds:
      default:
        return "buffer out-of-bounds access";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "check indices, sizes, and buffer boundaries";
    }

    std::vector<std::string> source_patterns_for_buffer_overflow()
    {
      return {
          ".at(",
          ".data()",
          "memcpy(",
          "memmove(",
          "strcpy(",
          "strncpy(",
          "sprintf(",
          "std::copy",
          "std::span",
          "[",
      };
    }

    bool looks_like_buffer_overflow_log(const std::string &log)
    {
      if (icontains(log, "heap-buffer-overflow") ||
          icontains(log, "stack-buffer-overflow") ||
          icontains(log, "global-buffer-overflow") ||
          icontains(log, "buffer-overflow"))
      {
        return true;
      }

      if (icontains(log, "out of bounds") ||
          icontains(log, "out-of-bounds"))
      {
        return true;
      }

      return false;
    }
  } // namespace

  class BufferOverflowRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_buffer_overflow_log(log);
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
            source_patterns_for_buffer_overflow());
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
              "prefer .at(), std::span, or range-checked containers over raw indexing",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeBufferOverflowRule()
  {
    return std::make_unique<BufferOverflowRule>();
  }
} // namespace vix::cli::errors::runtime
