/**
 *
 *  @file StackOverflowRule.cpp
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
    enum class StackOverflowKind
    {
      RecursiveCallChain,
      LargeStackAllocation,
      AddressSanitizerStackOverflow,
      GenericStackOverflow,
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

    StackOverflowKind classify_issue(
        const std::string &log)
    {
      if (icontains(log, "infinite recursion") ||
          icontains(log, "recursive call") ||
          icontains(log, "recursion depth") ||
          icontains(log, "deep recursion"))
      {
        return StackOverflowKind::RecursiveCallChain;
      }

      if (icontains(log, "large stack frame") ||
          icontains(log, "stack allocation") ||
          icontains(log, "stack object") ||
          icontains(log, "alloca"))
      {
        return StackOverflowKind::LargeStackAllocation;
      }

      if (icontains(log, "AddressSanitizer") &&
          (icontains(log, "stack-overflow") ||
           icontains(log, "stack overflow")))
      {
        return StackOverflowKind::
            AddressSanitizerStackOverflow;
      }

      return StackOverflowKind::GenericStackOverflow;
    }

    std::string choose_message(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case StackOverflowKind::RecursiveCallChain:
        return "stack exhausted by recursive calls";

      case StackOverflowKind::LargeStackAllocation:
        return "stack exhausted by a large local allocation";

      case StackOverflowKind::AddressSanitizerStackOverflow:
      case StackOverflowKind::GenericStackOverflow:
      default:
        return "stack overflow";
      }
    }

    std::string choose_description(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case StackOverflowKind::RecursiveCallChain:
        return "The call stack kept growing because a recursive call chain did not stop soon enough.";

      case StackOverflowKind::LargeStackAllocation:
        return "A local object required more stack memory than the current thread could provide.";

      case StackOverflowKind::AddressSanitizerStackOverflow:
      case StackOverflowKind::GenericStackOverflow:
      default:
        return "The current thread exhausted its available stack memory.";
      }
    }

    std::string choose_hint(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case StackOverflowKind::RecursiveCallChain:
        return "check the recursion base case and make sure every recursive call moves toward it";

      case StackOverflowKind::LargeStackAllocation:
        return "move large buffers or containers to the heap and reduce large local object sizes";

      case StackOverflowKind::AddressSanitizerStackOverflow:
      case StackOverflowKind::GenericStackOverflow:
      default:
        return "reduce recursion depth or move large local objects to the heap";
      }
    }

    std::string choose_secondary_hint(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case StackOverflowKind::RecursiveCallChain:
        return "consider replacing deep recursion with an iterative algorithm";

      case StackOverflowKind::AddressSanitizerStackOverflow:
        return "run with VIX_LOG_LEVEL=debug to inspect the repeated call frames";

      case StackOverflowKind::LargeStackAllocation:
      case StackOverflowKind::GenericStackOverflow:
      default:
        return {};
      }
    }

    bool may_use_source_hint(
        StackOverflowKind kind)
    {
      /*
       * A recursion location cannot be found reliably by scanning
       * for return statements or ordinary function calls.
       *
       * Source fallback is therefore allowed only for explicit
       * stack-allocation operations.
       */
      return kind ==
             StackOverflowKind::LargeStackAllocation;
    }

    std::vector<std::string>
    source_patterns_for_large_stack_allocation()
    {
      return {
          "alloca(",
          "std::array<",
      };
    }

    bool looks_like_stack_overflow_log(
        const std::string &log)
    {
      return icontains(log, "stack-overflow") ||
             icontains(log, "stack overflow") ||
             icontains(log, "stack exhausted");
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

  class StackOverflowRule final
      : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return looks_like_stack_overflow_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const StackOverflowKind kind =
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
                source_patterns_for_large_stack_allocation());
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
  makeStackOverflowRule()
  {
    return std::make_unique<
        StackOverflowRule>();
  }
} // namespace vix::cli::errors::runtime
