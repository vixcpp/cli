/**
 *
 *  @file BadVariantAccessRule.cpp
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
    bool looks_like_bad_variant_access_log(
        const std::string &log)
    {
      return icontains(log, "std::bad_variant_access") ||
             icontains(log, "bad_variant_access") ||
             icontains(log, "bad variant access") ||
             icontains(log, "std::get: wrong index for variant") ||
             icontains(log, "wrong index for variant");
    }

    std::vector<std::string>
    source_patterns_for_bad_variant_access()
    {
      /*
       * std::bad_variant_access is normally produced by std::get.
       *
       * Do not search for .value(): that operation belongs mainly
       * to std::optional and could highlight an unrelated line.
       */
      return {
          "std::get<",
          "std::get(",
      };
    }

    std::string choose_message()
    {
      return "variant holds a different value";
    }

    std::string choose_description()
    {
      return "The requested type is not currently active in this std::variant.";
    }

    std::string choose_hint()
    {
      return "check the active alternative with std::holds_alternative<T>() or use std::get_if<T>()";
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

    void print_hint_and_location(
        const std::string &hint,
        const RuntimeLocation &location)
    {
      std::vector<std::string> hints;

      if (!hint.empty())
        hints.push_back(hint);

      /*
       * Print "at:" only when Vix found a real or useful
       * source location.
       *
       * Do not fall back to "source: main.cpp", because the
       * source file alone is not an error location.
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
  } // namespace

  class BadVariantAccessRule final
      : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return looks_like_bad_variant_access_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const std::string message =
          choose_message();

      const std::string description =
          choose_description();

      RuntimeLocation location =
          find_best_runtime_location(
              log,
              sourceFile);

      if (!location.valid())
      {
        /*
         * Preserve the existing runtime-location contract.
         *
         * When the runtime provides no location, search only
         * for operations that can actually throw
         * std::bad_variant_access.
         */
        location =
            find_best_runtime_location_or_source_hint(
                log,
                sourceFile,
                source_patterns_for_bad_variant_access());
      }

      print_error(
          message,
          description);

      if (location.valid())
      {
        const auto error =
            make_runtime_location(
                location.file,
                location.line,
                location.column,
                message);

        print_runtime_codeframe(error);
      }

      print_hint_and_location(
          choose_hint(),
          location);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule>
  makeBadVariantAccessRule()
  {
    return std::make_unique<
        BadVariantAccessRule>();
  }
} // namespace vix::cli::errors::runtime
