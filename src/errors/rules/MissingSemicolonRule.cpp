/**
 *
 *  @file MissingSemicolonRule.cpp
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
#include <vix/cli/errors/IErrorRule.hpp>
#include <vix/cli/errors/CodeFrame.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
  namespace
  {
    std::string to_lower_ascii(std::string text)
    {
      std::transform(
          text.begin(),
          text.end(),
          text.begin(),
          [](unsigned char c)
          {
            return static_cast<char>(std::tolower(c));
          });

      return text;
    }
  } // namespace

  class MissingSemicolonRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string message = to_lower_ascii(err.message);

      const bool hasExpected =
          message.find("expected") != std::string::npos;

      const bool hasSemicolonToken =
          message.find("';'") != std::string::npos ||
          message.find("‘;’") != std::string::npos ||
          message.find("semicolon") != std::string::npos;

      const bool hasBeforeFallback =
          message.find(";") != std::string::npos &&
          message.find("before") != std::string::npos;

      return hasExpected && (hasSemicolonToken || hasBeforeFallback);
    }

    bool handle(
        const CompilerError &err,
        const ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: missing ';'"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "add a semicolon at the end of the statement, often on the previous line"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeMissingSemicolonRule()
  {
    return std::make_unique<MissingSemicolonRule>();
  }
} // namespace vix::cli::errors
