/**
 *
 *  @file OutOfRangeAccessRule.cpp
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
      if (icontains(log, "vector"))
        return "vector out-of-range access";

      if (icontains(log, "string") ||
          icontains(log, "basic_string"))
        return "string out-of-range access";

      if (icontains(log, "map::at"))
        return "missing key in map::at";

      if (icontains(log, "array"))
        return "array out-of-range access";

      if (icontains(log, "deque"))
        return "deque out-of-range access";

      return "out-of-range access";
    }

    std::string choose_hint(const std::string &log)
    {
      if (icontains(log, "vector"))
        return "check vector indices against size() before accessing elements";

      if (icontains(log, "string") ||
          icontains(log, "basic_string"))
        return "check string positions before calling at(), substr(), erase(), or insert()";

      if (icontains(log, "map::at"))
        return "check contains() or find() before calling map::at()";

      if (icontains(log, "array"))
        return "keep the index within the fixed array size before calling at()";

      if (icontains(log, "deque"))
        return "check deque indices against size() before accessing elements";

      return "check indices, container size, loop bounds, and calls to at()";
    }
  } // namespace

  class OutOfRangeAccessRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return icontains(log, "out_of_range") ||
             icontains(log, "std::out_of_range") ||
             icontains(log, "vector::_m_range_check") ||
             icontains(log, "basic_string::at") ||
             icontains(log, "map::at") ||
             icontains(log, "array::at") ||
             icontains(log, "deque::_m_range_check") ||
             (icontains(log, "out of range") &&
              (icontains(log, "vector") ||
               icontains(log, "string") ||
               icontains(log, "deque") ||
               icontains(log, "map") ||
               icontains(log, "array")));
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
                  ".at(",
                  "at(",
                  "operator[]",
                  "[",
                  "substr(",
                  "erase(",
                  "insert(",
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

  std::unique_ptr<IRuntimeErrorRule> makeOutOfRangeAccessRule()
  {
    return std::make_unique<OutOfRangeAccessRule>();
  }
} // namespace vix::cli::errors::runtime
