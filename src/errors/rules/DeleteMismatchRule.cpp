/**
 *
 *  @file DeleteMismatchRule.cpp
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

  class DeleteMismatchRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string message = to_lower_ascii(err.message);

      const bool hasMismatch =
          message.find("mismatched delete") != std::string::npos ||
          message.find("mismatched-new-delete") != std::string::npos ||
          message.find("mismatched new/delete") != std::string::npos ||
          message.find("alloc-dealloc-mismatch") != std::string::npos ||
          message.find("new-delete-type-mismatch") != std::string::npos;

      const bool hasNewArray =
          message.find("new[]") != std::string::npos ||
          message.find("operator new[]") != std::string::npos;

      const bool hasDeleteArray =
          message.find("delete[]") != std::string::npos ||
          message.find("operator delete[]") != std::string::npos;

      const bool hasPlainNew =
          message.find("operator new") != std::string::npos ||
          message.find("allocated with new") != std::string::npos;

      const bool hasPlainDelete =
          message.find("operator delete") != std::string::npos ||
          message.find("delete") != std::string::npos;

      const bool mismatchPair =
          (hasNewArray && hasPlainDelete) ||
          (hasPlainNew && hasDeleteArray);

      return hasMismatch || mismatchPair;
    }

    bool handle(
        const CompilerError &err,
        const ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: mismatched delete"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "match allocation and deallocation: new/delete, new[]/delete[], malloc/free"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeDeleteMismatchRule()
  {
    return std::make_unique<DeleteMismatchRule>();
  }
} // namespace vix::cli::errors
