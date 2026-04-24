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
#include <vector>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
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
      std::string title = "runtime error: out-of-range access";
      std::vector<std::string> hints = {
          "an index or key access went outside the valid range of the container",
          "check indices, container size, loop bounds, and calls to at() before accessing elements",
      };

      if (icontains(log, "vector"))
      {
        title = "runtime error: vector out-of-range access";
        hints = {
            "a std::vector access went past the valid index range",
            "check vector indices against size() and avoid using stale indices after erase or resize",
        };
      }
      else if (icontains(log, "string"))
      {
        title = "runtime error: string out-of-range access";
        hints = {
            "a std::string access used an invalid position",
            "check string positions before calling at(), substr(), erase(), or insert()",
        };
      }
      else if (icontains(log, "map::at"))
      {
        title = "runtime error: missing key in map::at";
        hints = {
            "map::at() was called with a key that does not exist",
            "check contains() first or use find() when the key may be absent",
        };
      }
      else if (icontains(log, "array"))
      {
        title = "runtime error: array out-of-range access";
        hints = {
            "a fixed-size array access used an invalid index",
            "verify bounds before calling at() and keep indices within the array size",
        };
      }
      else if (icontains(log, "deque"))
      {
        title = "runtime error: deque out-of-range access";
        hints = {
            "a std::deque access used an invalid index",
            "check bounds carefully and ensure the deque still has the expected size before access",
        };
      }

      std::cerr << RED
                << title
                << RESET << "\n";

      print_runtime_hints_and_at(
          hints,
          !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeOutOfRangeAccessRule()
  {
    return std::make_unique<OutOfRangeAccessRule>();
  }
} // namespace vix::cli::errors::runtime
