/**
 *
 *  @file InvalidCastRule.cpp
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
    enum class InvalidCastKind
    {
      InvalidVptr,
      DowncastOfAddress,
      MemberCallOnAddress,
      BadDynamicCast,
      BadAnyCast,
      GenericInvalidCast,
    };

    InvalidCastKind classify_issue(const std::string &log)
    {
      if (icontains(log, "invalid vptr"))
        return InvalidCastKind::InvalidVptr;

      if (icontains(log, "downcast of address"))
        return InvalidCastKind::DowncastOfAddress;

      if (icontains(log, "member call on address"))
        return InvalidCastKind::MemberCallOnAddress;

      if (icontains(log, "bad_cast") ||
          (icontains(log, "dynamic_cast") && icontains(log, "what():")))
      {
        return InvalidCastKind::BadDynamicCast;
      }

      if (icontains(log, "bad_any_cast"))
        return InvalidCastKind::BadAnyCast;

      return InvalidCastKind::GenericInvalidCast;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case InvalidCastKind::InvalidVptr:
      case InvalidCastKind::DowncastOfAddress:
      case InvalidCastKind::MemberCallOnAddress:
        return "invalid virtual object access";

      case InvalidCastKind::BadDynamicCast:
        return "bad dynamic cast";

      case InvalidCastKind::BadAnyCast:
      case InvalidCastKind::GenericInvalidCast:
      default:
        return "invalid cast";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "check object lifetime, real dynamic type, and virtual destructor usage";
    }

    std::vector<std::string> source_patterns_for_invalid_cast()
    {
      return {
          "dynamic_cast",
          "static_cast",
          "reinterpret_cast",
          "std::any_cast",
          "std::get<",
      };
    }

    bool looks_like_invalid_cast_log(const std::string &log)
    {
      if (icontains(log, "invalid vptr") ||
          icontains(log, "downcast of address") ||
          icontains(log, "member call on address"))
      {
        return true;
      }

      if (icontains(log, "bad_cast") ||
          icontains(log, "bad_any_cast"))
      {
        return true;
      }

      return false;
    }
  } // namespace

  class InvalidCastRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_invalid_cast_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const std::string message = choose_message(log);

      RuntimeLocation location = find_best_runtime_location(log, sourceFile);

      if (!location.valid())
      {
        location = find_best_runtime_location_or_source_hint(
            log,
            sourceFile,
            source_patterns_for_invalid_cast());
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
              "prefer dynamic_cast on pointers (returns nullptr) when the type is uncertain",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeInvalidCastRule()
  {
    return std::make_unique<InvalidCastRule>();
  }
} // namespace vix::cli::errors::runtime
