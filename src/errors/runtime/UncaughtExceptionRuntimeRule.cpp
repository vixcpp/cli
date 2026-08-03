/**
 *
 *  @file UncaughtExceptionRuntimeRule.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the LICENSE file.
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

    std::string extract_exception_type(
        const std::string &log)
    {
      static const std::string marker =
          "terminate called after throwing an instance of";

      const std::size_t markerPosition =
          log.find(marker);

      if (markerPosition == std::string::npos)
        return {};

      const std::size_t quoteBegin =
          log.find(
              '\'',
              markerPosition + marker.size());

      if (quoteBegin == std::string::npos)
        return {};

      const std::size_t quoteEnd =
          log.find('\'', quoteBegin + 1);

      if (quoteEnd == std::string::npos ||
          quoteEnd <= quoteBegin + 1)
      {
        return {};
      }

      return trim_copy(
          log.substr(
              quoteBegin + 1,
              quoteEnd - quoteBegin - 1));
    }

    std::string extract_exception_message(
        const std::string &log)
    {
      static const std::string marker = "what():";

      const std::size_t markerPosition =
          log.find(marker);

      if (markerPosition == std::string::npos)
        return {};

      const std::size_t messageBegin =
          markerPosition + marker.size();

      const std::size_t messageEnd =
          log.find_first_of(
              "\r\n",
              messageBegin);

      if (messageEnd == std::string::npos)
      {
        return trim_copy(
            log.substr(messageBegin));
      }

      return trim_copy(
          log.substr(
              messageBegin,
              messageEnd - messageBegin));
    }

    std::string friendly_title_for_exception(
        const std::string &exceptionType)
    {
      /*
       * Check specific exception types before their base types.
       *
       * For example, std::out_of_range derives from
       * std::logic_error, so out_of_range must be checked first.
       */

      if (icontains(exceptionType, "invalid_argument"))
        return "invalid value";

      if (icontains(exceptionType, "domain_error"))
        return "value is not allowed";

      if (icontains(exceptionType, "length_error"))
        return "requested size is too large";

      if (icontains(exceptionType, "out_of_range"))
        return "value is out of range";

      if (icontains(exceptionType, "overflow_error"))
        return "numeric value is too large";

      if (icontains(exceptionType, "underflow_error"))
        return "numeric value is too small";

      if (icontains(exceptionType, "range_error"))
        return "numeric value is out of range";

      if (icontains(exceptionType, "bad_array_new_length"))
        return "invalid array size";

      if (icontains(exceptionType, "bad_alloc"))
        return "not enough memory";

      if (icontains(exceptionType, "bad_optional_access"))
        return "required value is missing";

      if (icontains(exceptionType, "bad_variant_access"))
        return "value has an unexpected type";

      if (icontains(exceptionType, "bad_any_cast"))
        return "value has an unexpected type";

      if (icontains(exceptionType, "bad_function_call"))
        return "function is not available";

      if (icontains(exceptionType, "bad_weak_ptr"))
        return "object is no longer available";

      if (icontains(exceptionType, "bad_cast"))
        return "invalid type conversion";

      if (icontains(exceptionType, "bad_typeid"))
        return "object type is unavailable";

      if (icontains(exceptionType, "filesystem_error"))
        return "filesystem operation failed";

      if (icontains(exceptionType, "ios_base") ||
          icontains(exceptionType, "ios_failure"))
      {
        return "input or output operation failed";
      }

      if (icontains(exceptionType, "regex_error"))
        return "invalid regular expression";

      if (icontains(exceptionType, "future_error"))
        return "asynchronous operation failed";

      if (icontains(exceptionType, "system_error"))
        return "system operation failed";

      if (icontains(exceptionType, "logic_error"))
        return "operation cannot continue";

      if (icontains(exceptionType, "runtime_error"))
        return "operation failed";

      if (!exceptionType.empty())
        return "operation failed";

      return "application stopped unexpectedly";
    }

    std::string friendly_help_for_exception(
        const std::string &exceptionType)
    {
      if (icontains(exceptionType, "invalid_argument"))
      {
        return "check the value passed to this operation";
      }

      if (icontains(exceptionType, "domain_error"))
      {
        return "check that the value is allowed for this operation";
      }

      if (icontains(exceptionType, "length_error"))
      {
        return "check the requested size and its calculation";
      }

      if (icontains(exceptionType, "out_of_range"))
      {
        return "check that the index or value is within the allowed range";
      }

      if (icontains(exceptionType, "overflow_error"))
      {
        return "check the calculation or use a larger numeric type";
      }

      if (icontains(exceptionType, "underflow_error"))
      {
        return "check very small numeric values and calculations";
      }

      if (icontains(exceptionType, "range_error"))
      {
        return "check that the result is within the supported range";
      }

      if (icontains(exceptionType, "bad_array_new_length"))
      {
        return "check that the array size is valid";
      }

      if (icontains(exceptionType, "bad_alloc"))
      {
        return "reduce memory usage or check for excessive allocations";
      }

      if (icontains(exceptionType, "bad_optional_access"))
      {
        return "check that the optional contains a value before reading it";
      }

      if (icontains(exceptionType, "bad_variant_access"))
      {
        return "check the stored type before reading the variant";
      }

      if (icontains(exceptionType, "bad_any_cast"))
      {
        return "check the stored type before converting the value";
      }

      if (icontains(exceptionType, "bad_function_call"))
      {
        return "check that the function is valid before calling it";
      }

      if (icontains(exceptionType, "bad_weak_ptr"))
      {
        return "check that shared ownership still exists";
      }

      if (icontains(exceptionType, "bad_cast"))
      {
        return "check the object type before converting it";
      }

      if (icontains(exceptionType, "bad_typeid"))
      {
        return "check that the object exists before reading its type";
      }

      if (icontains(exceptionType, "filesystem_error"))
      {
        return "check the path, permissions, and filesystem state";
      }

      if (icontains(exceptionType, "ios_base") ||
          icontains(exceptionType, "ios_failure"))
      {
        return "check the file, stream, path, and permissions";
      }

      if (icontains(exceptionType, "regex_error"))
      {
        return "check the regular expression syntax";
      }

      if (icontains(exceptionType, "future_error"))
      {
        return "check the promise or future state";
      }

      if (icontains(exceptionType, "system_error"))
      {
        return "check the operation and the related system resources";
      }

      /*
       * logic_error, runtime_error and unknown exceptions usually
       * provide their most useful explanation through what().
       *
       * Do not add a generic hint that only repeats the message.
       */
      return {};
    }

    std::string fallback_description_for_exception(
        const std::string &exceptionType)
    {
      if (!exceptionType.empty())
        return "The program did not handle this error.";

      return "The application encountered an error it did not handle.";
    }

    bool looks_like_uncaught_exception_log(
        const std::string &log)
    {
      if (icontains(
              log,
              "terminate called after throwing an instance of"))
      {
        return true;
      }

      if (icontains(log, "uncaught exception"))
        return true;

      if (icontains(log, "what():") &&
          (icontains(log, "terminate called") ||
           icontains(log, "std::terminate") ||
           icontains(log, "Aborted")))
      {
        return true;
      }

      return false;
    }

    void print_error(
        const std::string &title,
        const std::string &reason,
        const std::string &exceptionType)
    {
      std::cerr << RED
                << "runtime error: "
                << title
                << RESET
                << "\n";

      if (!reason.empty())
      {
        std::cerr << "  "
                  << reason
                  << "\n";

        return;
      }

      const std::string fallback =
          fallback_description_for_exception(
              exceptionType);

      if (!fallback.empty())
      {
        std::cerr << "  "
                  << fallback
                  << "\n";
      }
    }

    void print_hint_and_location(
        const std::string &hint,
        const RuntimeLocation &location)
    {
      std::vector<std::string> hints;

      if (!hint.empty())
        hints.push_back(hint);

      /*
       * Do not use sourceFile as a fallback here.
       *
       * "source: main.cpp" is not an exact runtime location.
       * Print "at:" only when Vix has a real file and line.
       */
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
        const std::string &log,
        const std::string &exceptionType)
    {
      if (!technical_details_enabled())
        return;

      std::cerr << "\n"
                << GRAY
                << "technical details:"
                << RESET
                << "\n";

      if (!exceptionType.empty())
      {
        std::cerr << "  exception: "
                  << exceptionType
                  << "\n";
      }

      print_runtime_log_excerpt(
          log,
          20);
    }
  } // namespace

  class UncaughtExceptionRuntimeRule final
      : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return looks_like_uncaught_exception_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const std::string exceptionType =
          extract_exception_type(log);

      const std::string reason =
          extract_exception_message(log);

      const std::string title =
          friendly_title_for_exception(
              exceptionType);

      const std::string hint =
          friendly_help_for_exception(
              exceptionType);

      /*
       * Preserve the runtime-location contract.
       *
       * An uncaught exception has no reliable source location
       * unless the runtime output contains a real stack frame.
       *
       * Never search the source for generic words such as:
       *
       *   throw
       *   try
       *   catch
       *   logic_error
       *   runtime_error
       *
       * Such a search could highlight an unrelated line or even
       * interpret an executable as a source file.
       */
      const RuntimeLocation location =
          find_best_runtime_location(
              log,
              sourceFile);

      print_error(
          title,
          reason,
          exceptionType);

      if (location.valid())
      {
        const auto error =
            make_runtime_location(
                location.file,
                location.line,
                location.column,
                title);

        print_runtime_codeframe(error);
      }

      print_hint_and_location(
          hint,
          location);

      /*
       * Native C++ details remain available through:
       *
       * VIX_LOG_LEVEL=debug vix run ...
       */
      print_debug_details(
          log,
          exceptionType);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule>
  makeUncaughtExceptionRuntimeRule()
  {
    return std::make_unique<
        UncaughtExceptionRuntimeRule>();
  }
} // namespace vix::cli::errors::runtime
