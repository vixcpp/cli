/**
 *
 *  @file CoutNotDeclaredRule.cpp
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

  class CoutNotDeclaredRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string message = to_lower_ascii(err.message);

      if (message.find("cout") == std::string::npos)
        return false;

      const bool isUndeclared =
          message.find("undeclared identifier") != std::string::npos ||
          message.find("was not declared in this scope") != std::string::npos ||
          message.find("not declared in this scope") != std::string::npos;

      const bool isMissingStdMember =
          message.find("not a member of") != std::string::npos &&
          message.find("std") != std::string::npos;

      return isUndeclared || isMissingStdMember;
    }

    bool handle(
        const CompilerError &err,
        const ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: std::cout is not available"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "add #include <iostream> and use std::cout"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeCoutNotDeclaredRule()
  {
    return std::make_unique<CoutNotDeclaredRule>();
  }
} // namespace vix::cli::errors
