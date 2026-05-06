/**
 *
 *  @file StringViewDanglingRuntimeRule.cpp
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
        return "std::string_view outlived local string data";
      }

      if (icontains(log, "heap-use-after-free"))
        return "std::string_view points to freed memory";

      return "dangling std::string_view";
    }

    std::string choose_hint(const std::string &log)
    {
      if (icontains(log, "stack-use-after-scope") ||
          icontains(log, "use-after-return"))
      {
        return "return std::string when ownership is needed or ensure the viewed storage outlives the std::string_view";
      }

      if (icontains(log, "heap-use-after-free"))
        return "avoid keeping std::string_view after the owning string is destroyed, moved, cleared, or reallocated";

      return "do not keep std::string_view to temporary, local, destroyed, moved, or reallocated string storage";
    }
  } // namespace

  class StringViewDanglingRuntimeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return (icontains(log, "string_view") &&
              (icontains(log, "dangling") ||
               icontains(log, "lifetime") ||
               icontains(log, "invalid"))) ||
             (icontains(log, "basic_string_view") &&
              (icontains(log, "dangling") ||
               icontains(log, "invalid"))) ||
             (icontains(log, "stack-use-after-scope") && icontains(log, "string_view")) ||
             (icontains(log, "use-after-return") && icontains(log, "string_view")) ||
             (icontains(log, "heap-use-after-free") && icontains(log, "string_view"));
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
                  "std::string_view",
                  "string_view",
                  "basic_string_view",
                  ".substr(",
                  "substr(",
                  ".data()",
                  "data()",
                  "std::string",
                  "string ",
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

  std::unique_ptr<IRuntimeErrorRule> makeStringViewDanglingRuntimeRule()
  {
    return std::make_unique<StringViewDanglingRuntimeRule>();
  }
} // namespace vix::cli::errors::runtime
