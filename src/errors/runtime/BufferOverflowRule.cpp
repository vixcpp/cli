/**
 *
 *  @file BufferOverflowRule.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/errors/runtime/IRuntimeErrorRule.hpp>
#include <vix/cli/errors/runtime/RuntimeRuleUtils.hpp>

#include <cctype>
#include <cstdlib>
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
      DynamicStackBufferOverflow,
      GlobalBufferOverflow,
      ContainerOverflow,
      UbsanOutOfBounds,
      GenericBufferOverflow,
    };

    std::string trim_copy(const std::string &value)
    {
      std::size_t begin = 0;

      while (begin < value.size() &&
             std::isspace(
                 static_cast<unsigned char>(value[begin])) != 0)
      {
        ++begin;
      }

      std::size_t end = value.size();

      while (end > begin &&
             std::isspace(
                 static_cast<unsigned char>(value[end - 1])) != 0)
      {
        --end;
      }

      return value.substr(begin, end - begin);
    }

    std::string lowercase_copy(std::string value)
    {
      for (char &character : value)
      {
        character = static_cast<char>(
            std::tolower(
                static_cast<unsigned char>(character)));
      }

      return value;
    }

    bool technical_details_enabled()
    {
      const char *level = std::getenv("VIX_LOG_LEVEL");

      if (level == nullptr || *level == '\0')
        return false;

      const std::string value =
          lowercase_copy(trim_copy(level));

      return value == "debug" ||
             value == "trace";
    }

    bool looks_like_ubsan_index_error(
        const std::string &log)
    {
      const bool hasOutOfBounds =
          icontains(log, "out of bounds") ||
          icontains(log, "out-of-bounds");

      const bool hasIndexContext =
          icontains(log, "runtime error: index") ||
          icontains(log, "index ") ||
          icontains(log, "array index");

      return hasOutOfBounds &&
             hasIndexContext;
    }

    BufferOverflowKind classify_issue(
        const std::string &log)
    {
      /*
       * Check dynamic-stack-buffer-overflow before the more
       * general stack-buffer-overflow marker.
       */
      if (icontains(
              log,
              "dynamic-stack-buffer-overflow"))
      {
        return BufferOverflowKind::
            DynamicStackBufferOverflow;
      }

      if (icontains(log, "heap-buffer-overflow"))
      {
        return BufferOverflowKind::
            HeapBufferOverflow;
      }

      if (icontains(log, "stack-buffer-overflow"))
      {
        return BufferOverflowKind::
            StackBufferOverflow;
      }

      if (icontains(log, "global-buffer-overflow"))
      {
        return BufferOverflowKind::
            GlobalBufferOverflow;
      }

      if (icontains(log, "container-overflow"))
      {
        return BufferOverflowKind::
            ContainerOverflow;
      }

      if (looks_like_ubsan_index_error(log))
      {
        return BufferOverflowKind::
            UbsanOutOfBounds;
      }

      return BufferOverflowKind::
          GenericBufferOverflow;
    }

    std::string choose_message(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case BufferOverflowKind::HeapBufferOverflow:
        return "heap-buffer-overflow";

      case BufferOverflowKind::StackBufferOverflow:
        return "stack-buffer-overflow";

      case BufferOverflowKind::DynamicStackBufferOverflow:
        return "dynamic-stack-buffer-overflow";

      case BufferOverflowKind::GlobalBufferOverflow:
        return "global-buffer-overflow";

      case BufferOverflowKind::ContainerOverflow:
        return "container-overflow";

      case BufferOverflowKind::UbsanOutOfBounds:
        return "out-of-bounds access";

      case BufferOverflowKind::GenericBufferOverflow:
      default:
        return "buffer overflow";
      }
    }

    std::string choose_description(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case BufferOverflowKind::HeapBufferOverflow:
        return "The program accessed memory past the end of a dynamically allocated buffer.";

      case BufferOverflowKind::StackBufferOverflow:
        return "The program accessed memory past the end of a local fixed-size buffer.";

      case BufferOverflowKind::DynamicStackBufferOverflow:
        return "The program accessed memory past the end of a dynamically sized stack buffer.";

      case BufferOverflowKind::GlobalBufferOverflow:
        return "The program accessed memory past the end of a global or static buffer.";

      case BufferOverflowKind::ContainerOverflow:
        return "The program accessed memory outside the valid storage owned by a container.";

      case BufferOverflowKind::UbsanOutOfBounds:
        return "An index was outside the valid range of the array or buffer.";

      case BufferOverflowKind::GenericBufferOverflow:
      default:
        return "The program accessed memory outside the valid buffer range.";
      }
    }

    std::string choose_hint(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case BufferOverflowKind::HeapBufferOverflow:
        return "check heap buffer, vector, string, allocation, and copy sizes";

      case BufferOverflowKind::StackBufferOverflow:
        return "check local array bounds, indexes, and copy lengths";

      case BufferOverflowKind::DynamicStackBufferOverflow:
        return "check the dynamic stack allocation size and every index used with it";

      case BufferOverflowKind::GlobalBufferOverflow:
        return "check global or static array bounds and copy lengths";

      case BufferOverflowKind::ContainerOverflow:
        return "check container size, capacity, iterators, and copy destinations";

      case BufferOverflowKind::UbsanOutOfBounds:
        return "verify that the index is smaller than the array or buffer size";

      case BufferOverflowKind::GenericBufferOverflow:
      default:
        return "check indexes, allocation sizes, and buffer boundaries";
      }
    }

    std::string choose_secondary_hint(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case BufferOverflowKind::HeapBufferOverflow:
      case BufferOverflowKind::StackBufferOverflow:
      case BufferOverflowKind::DynamicStackBufferOverflow:
      case BufferOverflowKind::GlobalBufferOverflow:
      case BufferOverflowKind::ContainerOverflow:
        return "make sure every copy length is no larger than the destination capacity";

      case BufferOverflowKind::UbsanOutOfBounds:
      case BufferOverflowKind::GenericBufferOverflow:
      default:
        return {};
      }
    }

    bool may_use_source_hint(
        BufferOverflowKind kind)
    {
      /*
       * The copy-operation fallback is useful for explicit buffer
       * overflows, but not for an arbitrary UBSan index failure.
       */
      return kind !=
             BufferOverflowKind::UbsanOutOfBounds;
    }

    std::vector<std::string>
    source_patterns_for_buffer_overflow()
    {
      /*
       * Keep only high-signal unchecked memory operations.
       *
       * "[" and ".at(" are intentionally excluded.
       */
      return {
          "memcpy(",
          "memmove(",
          "memset(",
          "strcpy(",
          "strncpy(",
          "strcat(",
          "strncat(",
          "sprintf(",
          "snprintf(",
          "std::copy(",
          "std::copy_n(",
          "std::ranges::copy(",
      };
    }

    bool looks_like_buffer_overflow_log(
        const std::string &log)
    {
      if (icontains(log, "heap-buffer-overflow") ||
          icontains(log, "stack-buffer-overflow") ||
          icontains(log, "dynamic-stack-buffer-overflow") ||
          icontains(log, "global-buffer-overflow") ||
          icontains(log, "container-overflow") ||
          icontains(log, "buffer-overflow") ||
          icontains(log, "buffer overflow"))
      {
        return true;
      }

      return looks_like_ubsan_index_error(log);
    }

    void print_error(
        const std::string &message,
        const std::string &description)
    {
      std::cerr << RED
                << "runtime error: "
                << message
                << RESET
                << "\n";

      if (!description.empty())
      {
        std::cerr << "  "
                  << description
                  << "\n";
      }
    }

    void print_hints_and_location(
        const std::string &hint,
        const std::string &secondaryHint,
        const RuntimeLocation &location)
    {
      std::vector<std::string> hints;

      if (!hint.empty())
        hints.push_back(hint);

      if (!secondaryHint.empty())
        hints.push_back(secondaryHint);

      const std::string at =
          location.valid()
              ? make_at_text(
                    location,
                    std::filesystem::path{})
              : std::string{};

      if (hints.empty() && at.empty())
        return;

      std::cerr << "\n";

      print_runtime_hints_and_at(
          hints,
          at);
    }

    void print_debug_details(
        const std::string &log)
    {
      if (!technical_details_enabled())
        return;

      std::cerr << "\n"
                << GRAY
                << "technical details:"
                << RESET
                << "\n";

      print_runtime_log_excerpt(
          log,
          24);
    }
  } // namespace

  class BufferOverflowRule final
      : public IRuntimeErrorRule
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
      const BufferOverflowKind kind =
          classify_issue(log);

      const std::string message =
          choose_message(log);

      const std::string description =
          choose_description(log);

      const std::string hint =
          choose_hint(log);

      const std::string secondaryHint =
          choose_secondary_hint(log);

      RuntimeLocation location =
          find_best_runtime_location(
              log,
              sourceFile);

      if (!location.valid() &&
          may_use_source_hint(kind))
      {
        location =
            find_best_runtime_location_or_source_hint(
                log,
                sourceFile,
                source_patterns_for_buffer_overflow());
      }

      print_error(
          message,
          description);

      if (location.valid())
      {
        std::cerr << "\n";

        const auto error =
            make_runtime_location(
                location.file,
                location.line,
                location.column,
                message);

        print_runtime_codeframe(error);
      }

      print_hints_and_location(
          hint,
          secondaryHint,
          location);

      print_debug_details(log);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule>
  makeBufferOverflowRule()
  {
    return std::make_unique<
        BufferOverflowRule>();
  }
} // namespace vix::cli::errors::runtime
