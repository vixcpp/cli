/**
 *
 *  @file ReturnLocalRefRule.cpp
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

  class ReturnLocalRefRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string message = to_lower_ascii(err.message);

      const bool mentionsReturn =
          message.find("return") != std::string::npos ||
          message.find("returned") != std::string::npos ||
          message.find("returning") != std::string::npos;

      const bool localAddress =
          message.find("address of local") != std::string::npos ||
          message.find("address of stack memory") != std::string::npos ||
          message.find("reference to local") != std::string::npos ||
          message.find("reference to stack") != std::string::npos ||
          (message.find("local variable") != std::string::npos &&
           message.find("returned") != std::string::npos);

      const bool stackLifetime =
          message.find("stack memory") != std::string::npos ||
          message.find("temporary") != std::string::npos ||
          message.find("does not live long enough") != std::string::npos;

      return mentionsReturn && (localAddress || stackLifetime);
    }

    bool handle(
        const CompilerError &err,
        const ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: returning local object reference"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "return by value or ensure the referenced object outlives the function"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeReturnLocalRefRule()
  {
    return std::make_unique<ReturnLocalRefRule>();
  }
} // namespace vix::cli::errors
