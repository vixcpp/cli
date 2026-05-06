/**
 *
 *  @file UniquePtrCopyRule.cpp
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

  class UniquePtrCopyRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string message = to_lower_ascii(err.message);

      const bool mentionsUniquePtr =
          message.find("std::unique_ptr") != std::string::npos ||
          message.find("unique_ptr") != std::string::npos;

      if (!mentionsUniquePtr)
        return false;

      const bool deletedFunction =
          message.find("use of deleted function") != std::string::npos ||
          message.find("call to deleted constructor") != std::string::npos ||
          message.find("attempt to use a deleted function") != std::string::npos ||
          message.find("is deleted") != std::string::npos ||
          message.find("implicitly-deleted") != std::string::npos ||
          message.find("deleted constructor") != std::string::npos ||
          message.find("deleted copy constructor") != std::string::npos;

      const bool copyClue =
          message.find("copy") != std::string::npos ||
          message.find("copy constructor") != std::string::npos ||
          message.find("copy assignment") != std::string::npos ||
          message.find("cannot be copied") != std::string::npos;

      return deletedFunction || copyClue;
    }

    bool handle(
        const CompilerError &err,
        const ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: std::unique_ptr cannot be copied"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "use std::move to transfer ownership or pass the std::unique_ptr by reference"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeUniquePtrCopyRule()
  {
    return std::make_unique<UniquePtrCopyRule>();
  }
} // namespace vix::cli::errors
