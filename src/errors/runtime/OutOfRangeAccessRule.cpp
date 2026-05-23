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
  namespace
  {
    enum class OutOfRangeKind
    {
      Vector,
      StringAt,
      StringSubstr,
      MapAt,
      UnorderedMapAt,
      Array,
      Deque,
      Span,
      OperatorIndex,
      GenericOutOfBounds,
      GenericOutOfRange,
    };

    OutOfRangeKind classify_issue(const std::string &log)
    {
      if (icontains(log, "vector") ||
          icontains(log, "vector::_m_range_check") ||
          icontains(log, "vector::_M_range_check"))
      {
        return OutOfRangeKind::Vector;
      }

      if ((icontains(log, "basic_string") ||
           icontains(log, "string")) &&
          icontains(log, "substr"))
      {
        return OutOfRangeKind::StringSubstr;
      }

      if (icontains(log, "basic_string::at") ||
          (icontains(log, "string") && icontains(log, "at")))
      {
        return OutOfRangeKind::StringAt;
      }

      if (icontains(log, "unordered_map::at") ||
          icontains(log, "unordered map") ||
          icontains(log, "unordered_map"))
      {
        return OutOfRangeKind::UnorderedMapAt;
      }

      if (icontains(log, "map::at") ||
          (icontains(log, "map") && icontains(log, "at")))
      {
        return OutOfRangeKind::MapAt;
      }

      if (icontains(log, "array::at") ||
          icontains(log, "std::array") ||
          icontains(log, "array"))
      {
        return OutOfRangeKind::Array;
      }

      if (icontains(log, "deque") ||
          icontains(log, "deque::_m_range_check") ||
          icontains(log, "deque::_M_range_check"))
      {
        return OutOfRangeKind::Deque;
      }

      if (icontains(log, "span") ||
          icontains(log, "std::span"))
      {
        return OutOfRangeKind::Span;
      }

      if (icontains(log, "operator[]") ||
          icontains(log, "index") ||
          icontains(log, "subscript"))
      {
        return OutOfRangeKind::OperatorIndex;
      }

      if (icontains(log, "out of bounds") ||
          icontains(log, "out-of-bounds") ||
          icontains(log, "index out of range"))
      {
        return OutOfRangeKind::GenericOutOfBounds;
      }

      return OutOfRangeKind::GenericOutOfRange;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case OutOfRangeKind::Vector:
        return "vector out-of-range access";

      case OutOfRangeKind::StringAt:
        return "string out-of-range access";

      case OutOfRangeKind::StringSubstr:
        return "string substr() out-of-range access";

      case OutOfRangeKind::MapAt:
        return "missing key in map::at";

      case OutOfRangeKind::UnorderedMapAt:
        return "missing key in unordered_map::at";

      case OutOfRangeKind::Array:
        return "array out-of-range access";

      case OutOfRangeKind::Deque:
        return "deque out-of-range access";

      case OutOfRangeKind::Span:
        return "span out-of-range access";

      case OutOfRangeKind::OperatorIndex:
        return "index out-of-range access";

      case OutOfRangeKind::GenericOutOfBounds:
        return "out-of-bounds access";

      case OutOfRangeKind::GenericOutOfRange:
      default:
        return "out-of-range access";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case OutOfRangeKind::Vector:
        return "check vector indices against size() before accessing elements";

      case OutOfRangeKind::StringAt:
        return "check string positions before calling at(), erase(), or insert()";

      case OutOfRangeKind::StringSubstr:
        return "ensure the substr() start position is not greater than string.size()";

      case OutOfRangeKind::MapAt:
        return "check contains() or find() before calling map::at()";

      case OutOfRangeKind::UnorderedMapAt:
        return "check contains() or find() before calling unordered_map::at()";

      case OutOfRangeKind::Array:
        return "keep the index within the fixed array size before calling at()";

      case OutOfRangeKind::Deque:
        return "check deque indices against size() before accessing elements";

      case OutOfRangeKind::Span:
        return "check span indices against size() before indexing";

      case OutOfRangeKind::OperatorIndex:
        return "check the index before using operator[]; prefer at() while debugging";

      case OutOfRangeKind::GenericOutOfBounds:
        return "check indices, sizes, loop bounds, and signed/unsigned conversions";

      case OutOfRangeKind::GenericOutOfRange:
      default:
        return "check indices, container size, loop bounds, and calls to at()";
      }
    }

    std::vector<std::string> source_patterns_for_out_of_range()
    {
      return {
          ".at(",
          "at(",
          "operator[]",
          "[",
          ".substr(",
          "substr(",
          ".erase(",
          "erase(",
          ".insert(",
          "insert(",
          ".resize(",
          "resize(",
          ".reserve(",
          "reserve(",
          ".size(",
          "size()",
          ".contains(",
          "contains(",
          ".find(",
          "find(",
      };
    }

    bool looks_like_out_of_range_log(const std::string &log)
    {
      const bool explicitOutOfRange =
          icontains(log, "out_of_range") ||
          icontains(log, "std::out_of_range") ||
          icontains(log, "out of range") ||
          icontains(log, "out-of-range");

      const bool knownContainerMessage =
          icontains(log, "vector::_m_range_check") ||
          icontains(log, "vector::_M_range_check") ||
          icontains(log, "basic_string::at") ||
          icontains(log, "basic_string::substr") ||
          icontains(log, "map::at") ||
          icontains(log, "unordered_map::at") ||
          icontains(log, "array::at") ||
          icontains(log, "deque::_m_range_check") ||
          icontains(log, "deque::_M_range_check");

      const bool sanitizerBounds =
          icontains(log, "out of bounds") ||
          icontains(log, "out-of-bounds") ||
          icontains(log, "index out of range") ||
          icontains(log, "subscript") ||
          icontains(log, "runtime error: index");

      const bool containerContext =
          icontains(log, "vector") ||
          icontains(log, "string") ||
          icontains(log, "basic_string") ||
          icontains(log, "deque") ||
          icontains(log, "map") ||
          icontains(log, "unordered_map") ||
          icontains(log, "array") ||
          icontains(log, "span");

      return knownContainerMessage ||
             (explicitOutOfRange && containerContext) ||
             (sanitizerBounds && containerContext) ||
             (explicitOutOfRange && icontains(log, "what():"));
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
      return looks_like_out_of_range_log(log);
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
                source_patterns_for_out_of_range());
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
              "do not ignore the runtime log: out_of_range usually includes the invalid index and container size",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeOutOfRangeAccessRule()
  {
    return std::make_unique<OutOfRangeAccessRule>();
  }
} // namespace vix::cli::errors::runtime
