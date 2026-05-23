/**
 *
 *  @file SegfaultRule.cpp
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
    enum class SegfaultKind
    {
      NullPointer,
      InvalidAddress,
      UseAfterFree,
      StackOverflow,
      BufferOverflow,
      DanglingPointer,
      WriteToReadOnlyMemory,
      GenericSegfault,
    };

    SegfaultKind classify_segfault(const std::string &log)
    {
      if (icontains(log, "stack-overflow") ||
          icontains(log, "stack overflow"))
      {
        return SegfaultKind::StackOverflow;
      }

      if (icontains(log, "use-after-free") ||
          icontains(log, "heap-use-after-free"))
      {
        return SegfaultKind::UseAfterFree;
      }

      if (icontains(log, "null") ||
          icontains(log, "address 0x0") ||
          icontains(log, "addr 0x0") ||
          icontains(log, "nullptr") ||
          icontains(log, "zero page"))
      {
        return SegfaultKind::NullPointer;
      }

      if (icontains(log, "out of bounds") ||
          icontains(log, "out-of-bounds") ||
          icontains(log, "buffer-overflow") ||
          icontains(log, "buffer overflow") ||
          icontains(log, "heap-buffer-overflow") ||
          icontains(log, "stack-buffer-overflow"))
      {
        return SegfaultKind::BufferOverflow;
      }

      if (icontains(log, "dangling") ||
          icontains(log, "invalid pointer") ||
          icontains(log, "wild pointer"))
      {
        return SegfaultKind::DanglingPointer;
      }

      if (icontains(log, "read-only") ||
          icontains(log, "readonly") ||
          icontains(log, "write access") ||
          icontains(log, "SEGV_ACCERR"))
      {
        return SegfaultKind::WriteToReadOnlyMemory;
      }

      if (icontains(log, "SEGV_MAPERR") ||
          icontains(log, "invalid address") ||
          icontains(log, "bad address"))
      {
        return SegfaultKind::InvalidAddress;
      }

      return SegfaultKind::GenericSegfault;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_segfault(log))
      {
      case SegfaultKind::NullPointer:
        return "null pointer dereference";

      case SegfaultKind::InvalidAddress:
        return "invalid memory address access";

      case SegfaultKind::UseAfterFree:
        return "use-after-free caused segmentation fault";

      case SegfaultKind::StackOverflow:
        return "stack overflow";

      case SegfaultKind::BufferOverflow:
        return "buffer out-of-bounds access";

      case SegfaultKind::DanglingPointer:
        return "dangling pointer access";

      case SegfaultKind::WriteToReadOnlyMemory:
        return "invalid write to protected memory";

      case SegfaultKind::GenericSegfault:
      default:
        return "segmentation fault";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_segfault(log))
      {
      case SegfaultKind::NullPointer:
        return "check for null pointers before dereferencing them";

      case SegfaultKind::InvalidAddress:
        return "check pointer initialization, pointer arithmetic, and object lifetime";

      case SegfaultKind::UseAfterFree:
        return "check dangling pointers, references, and ownership after delete/free or object destruction";

      case SegfaultKind::StackOverflow:
        return "reduce recursion depth or move large local objects to the heap";

      case SegfaultKind::BufferOverflow:
        return "check array, vector, string, span, and raw buffer bounds before access";

      case SegfaultKind::DanglingPointer:
        return "ensure the referenced object is still alive before using the pointer or reference";

      case SegfaultKind::WriteToReadOnlyMemory:
        return "avoid writing through pointers to string literals, const data, or invalid memory regions";

      case SegfaultKind::GenericSegfault:
      default:
        return "check null pointers, dangling pointers, out-of-bounds access, and invalid references";
      }
    }

    std::vector<std::string> source_patterns_for_segfault()
    {
      return {
          "nullptr",
          "= nullptr",
          "->",
          "*",
          "[",
          ".at(",
          "delete",
          "delete[]",
          "free(",
          "malloc(",
          "new ",
          "std::unique_ptr",
          "std::shared_ptr",
          "std::weak_ptr",
          "std::span",
          "std::string_view",
          "reinterpret_cast",
          "static_cast",
          "const_cast",
      };
    }

    bool looks_like_segfault_log(const std::string &log)
    {
      return icontains(log, "segmentation fault") ||
             icontains(log, "sigsegv") ||
             icontains(log, "signal 11") ||
             icontains(log, "SEGV_MAPERR") ||
             icontains(log, "SEGV_ACCERR") ||
             icontains(log, "AddressSanitizer: SEGV") ||
             icontains(log, "ERROR: AddressSanitizer") ||
             (icontains(log, "runtime error") &&
              (icontains(log, "null pointer") ||
               icontains(log, "misaligned address") ||
               icontains(log, "invalid memory")));
    }
  } // namespace

  class SegfaultRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_segfault_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const std::string message = choose_message(log);

      RuntimeLocation location =
          find_best_runtime_location(log, sourceFile);

      if (!location.valid())
      {
        location =
            find_best_runtime_location_or_source_hint(
                log,
                sourceFile,
                source_patterns_for_segfault());
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
              "do not ignore the runtime log: segfault reports often include the real invalid address or sanitizer stack trace",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 24);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeSegfaultRule()
  {
    return std::make_unique<SegfaultRule>();
  }
} // namespace vix::cli::errors::runtime
