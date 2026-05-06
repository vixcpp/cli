/**
 *
 *  @file ProcessNullptrAmbiguityRule.cpp
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

  class ProcessNullptrAmbiguityRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string message = to_lower_ascii(err.message);

      const bool mentionsProcess =
          message.find("process") != std::string::npos;

      if (!mentionsProcess)
        return false;

      const bool noMatching =
          message.find("no matching function for call") != std::string::npos;

      const bool ambiguous =
          message.find("ambiguous") != std::string::npos;

      return noMatching || ambiguous;
    }

    bool handle(
        const CompilerError &err,
        const ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: ambiguous process call"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "use an explicit cast or exact argument type to select the intended overload"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeProcessNullptrAmbiguityRule()
  {
    return std::make_unique<ProcessNullptrAmbiguityRule>();
  }
} // namespace vix::cli::errors
