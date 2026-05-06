/**
 *
 *  @file VectorOstreamRule.cpp
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

  class VectorOstreamRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string message = to_lower_ascii(err.message);

      const bool hasVector =
          message.find("std::vector") != std::string::npos ||
          message.find("vector<") != std::string::npos;

      if (!hasVector)
        return false;

      const bool mentionsOperator =
          message.find("operator<<") != std::string::npos ||
          message.find("operator <<") != std::string::npos;

      const bool noMatchingOperator =
          message.find("no match for") != std::string::npos ||
          message.find("no matching") != std::string::npos ||
          message.find("invalid operands") != std::string::npos ||
          message.find("invalid operands to binary expression") != std::string::npos;

      const bool hasStream =
          message.find("std::ostream") != std::string::npos ||
          message.find("basic_ostream") != std::string::npos ||
          message.find("ostream") != std::string::npos;

      return noMatchingOperator && (mentionsOperator || hasStream);
    }

    bool handle(
        const CompilerError &err,
        const ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: cannot print std::vector directly"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "loop over the vector elements or define operator<< for your vector type"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeVectorOstreamRule()
  {
    return std::make_unique<VectorOstreamRule>();
  }
} // namespace vix::cli::errors
