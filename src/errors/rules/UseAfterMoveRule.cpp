/**
 *
 *  @file UseAfterMoveRule.cpp
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

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <regex>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

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

    std::string extract_moved_variable(const std::string &message)
    {
      {
        const std::regex re(R"(use of '([^']+)' after it was moved)");
        std::smatch match;

        if (std::regex_search(message, match, re) && match.size() >= 2)
          return match[1].str();
      }

      {
        const std::regex re(R"(use of moved-from variable '([^']+)')");
        std::smatch match;

        if (std::regex_search(message, match, re) && match.size() >= 2)
          return match[1].str();
      }

      return "object";
    }
  } // namespace

  class UseAfterMoveRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string message = to_lower_ascii(err.message);

      const bool afterMoved =
          message.find("after it was moved") != std::string::npos;

      const bool useOf =
          message.find("use of") != std::string::npos;

      const bool movedValue =
          message.find("use of moved") != std::string::npos ||
          message.find("moved-from") != std::string::npos ||
          message.find("use-after-move") != std::string::npos;

      return (afterMoved && useOf) || movedValue;
    }

    bool handle(
        const CompilerError &err,
        const ErrorContext &ctx) const override
    {
      const std::string variableName =
          extract_moved_variable(err.message);

      std::cerr << RED
                << "error: use-after-move"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "'" << variableName << "' was moved; reassign it before using it again"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeUseAfterMoveRule()
  {
    return std::make_unique<UseAfterMoveRule>();
  }
} // namespace vix::cli::errors
