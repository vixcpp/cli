/**
 *
 *  @file SpanLifetimeRule.cpp
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

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  namespace
  {
    std::string choose_message(const std::string &log)
    {
      if (icontains(log, "stack-use-after-scope") ||
          icontains(log, "use-after-return"))
      {
        return "std::span outlived local storage";
      }

      if (icontains(log, "heap-use-after-free"))
        return "std::span points to freed memory";

      return "dangling std::span";
    }

    std::string choose_hint(const std::string &log)
    {
      if (icontains(log, "stack-use-after-scope") ||
          icontains(log, "use-after-return"))
      {
        return "ensure the underlying local storage outlives the std::span";
      }

      if (icontains(log, "heap-use-after-free"))
        return "avoid keeping std::span after the owning container is destroyed, resized, or reallocated";

      return "ensure the array, vector, or buffer referenced by std::span stays alive";
    }
  } // namespace

  class SpanLifetimeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return (icontains(log, "span") &&
              (icontains(log, "dangling") ||
               icontains(log, "lifetime") ||
               icontains(log, "invalid"))) ||
             (icontains(log, "std::span") &&
              (icontains(log, "dangling") ||
               icontains(log, "invalid"))) ||
             (icontains(log, "stack-use-after-scope") &&
              (icontains(log, "span") || icontains(log, "std::span"))) ||
             (icontains(log, "use-after-return") &&
              (icontains(log, "span") || icontains(log, "std::span"))) ||
             (icontains(log, "heap-use-after-free") &&
              (icontains(log, "span") || icontains(log, "std::span")));
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const RuntimeLocation location =
          find_best_runtime_location_or_source_hint(
              log,
              sourceFile,
              {
                  "std::span",
                  "span<",
                  "span ",
                  ".data()",
                  "data()",
                  "resize(",
                  "clear(",
                  "delete",
                  "return",
              });

      const std::string message = choose_message(log);

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
          },
          make_at_text(location, sourceFile));

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeSpanLifetimeRule()
  {
    return std::make_unique<SpanLifetimeRule>();
  }
} // namespace vix::cli::errors::runtime
