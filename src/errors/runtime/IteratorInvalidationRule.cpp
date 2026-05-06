/**
 *
 *  @file IteratorInvalidationRule.cpp
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
      if (icontains(log, "dereference"))
        return "invalid iterator dereference";

      if (icontains(log, "increment"))
        return "invalid iterator increment";

      if (icontains(log, "compare"))
        return "invalid iterator comparison";

      return "iterator invalidation";
    }

    std::string choose_hint(const std::string &log)
    {
      if (icontains(log, "dereference"))
        return "refresh the iterator before dereferencing it after container modification";

      if (icontains(log, "increment"))
        return "re-acquire the iterator before incrementing it after container modification";

      if (icontains(log, "compare"))
        return "do not compare iterators that may have been invalidated by container modification";

      return "avoid reusing iterators after erase, insert, push_back, resize, reserve, or reallocation";
    }
  } // namespace

  class IteratorInvalidationRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return icontains(log, "invalidated iterator") ||
             icontains(log, "iterator invalidation") ||
             icontains(log, "cannot dereference invalidated") ||
             icontains(log, "attempt to dereference a singular iterator") ||
             icontains(log, "attempt to increment a singular iterator") ||
             icontains(log, "attempt to compare a singular iterator") ||
             (icontains(log, "vector iterator") && icontains(log, "invalid")) ||
             (icontains(log, "deque iterator") && icontains(log, "invalid")) ||
             (icontains(log, "list iterator") && icontains(log, "invalid"));
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const RuntimeLocation location =
          find_best_runtime_location(log, sourceFile);

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

  std::unique_ptr<IRuntimeErrorRule> makeIteratorInvalidationRule()
  {
    return std::make_unique<IteratorInvalidationRule>();
  }
} // namespace vix::cli::errors::runtime
