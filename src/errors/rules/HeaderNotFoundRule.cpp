/**
 *
 *  @file HeaderNotFoundRule.cpp
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

  class HeaderNotFoundRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string message = to_lower_ascii(err.message);

      const bool hasNotFound =
          message.find("file not found") != std::string::npos ||
          message.find("no such file or directory") != std::string::npos ||
          message.find("cannot open include file") != std::string::npos;

      const bool looksLikeHeader =
          message.find(".hpp") != std::string::npos ||
          message.find(".hh") != std::string::npos ||
          message.find(".hxx") != std::string::npos ||
          message.find(".h") != std::string::npos;

      return hasNotFound && looksLikeHeader;
    }

    bool handle(
        const CompilerError &err,
        const ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: header file not found"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "check the include path and make sure the header exists"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeHeaderNotFoundRule()
  {
    return std::make_unique<HeaderNotFoundRule>();
  }
} // namespace vix::cli::errors
