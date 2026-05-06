/**
 *
 *  @file UseOfUninitializedRule.cpp
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

  class UseOfUninitializedRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string message = to_lower_ascii(err.message);

      const bool mentionsUninitialized =
          message.find("uninitialized") != std::string::npos;

      if (!mentionsUninitialized)
        return false;

      const bool strongPhrase =
          message.find("may be used") != std::string::npos ||
          message.find("is used") != std::string::npos ||
          message.find("use of uninitialized") != std::string::npos ||
          message.find("uninitialized use") != std::string::npos ||
          message.find("used uninitialized") != std::string::npos ||
          message.find("maybe-uninitialized") != std::string::npos;

      return strongPhrase;
    }

    bool handle(
        const CompilerError &err,
        const ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: uninitialized value"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "initialize the variable before reading or passing it"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeUseOfUninitializedRule()
  {
    return std::make_unique<UseOfUninitializedRule>();
  }
} // namespace vix::cli::errors
