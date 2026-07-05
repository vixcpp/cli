/**
 *
 *  @file JsonParseRuntimeRule.cpp
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
    std::string choose_message(const std::string &log)
    {
      (void)log;
      return "JSON parse failed";
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "validate JSON syntax and check the exact line/column from the parser";
    }

    std::vector<std::string> source_patterns_for_json()
    {
      return {
          "nlohmann::json",
          "json::parse",
          ".parse(",
          "from_json",
          "to_json",
      };
    }

    bool looks_like_json_parse_log(const std::string &log)
    {
      if (icontains(log, "json parse error") ||
          icontains(log, "parse_error") ||
          icontains(log, "nlohmann::json"))
      {
        return true;
      }

      if (icontains(log, "invalid JSON"))
        return true;

      // "unexpected token" + JSON context only, to avoid false positives.
      if (icontains(log, "unexpected token") &&
          (icontains(log, "json") || icontains(log, "JSON")))
      {
        return true;
      }

      return false;
    }
  } // namespace

  class JsonParseRuntimeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_json_parse_log(log);
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
            source_patterns_for_json());
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
              "validate the document with a JSON linter before parsing untrusted input",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeJsonParseRuntimeRule()
  {
    return std::make_unique<JsonParseRuntimeRule>();
  }
} // namespace vix::cli::errors::runtime
