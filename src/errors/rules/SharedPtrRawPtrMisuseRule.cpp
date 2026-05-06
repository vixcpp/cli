/**
 *
 *  @file SharedPtrRawPtrMisuseRule.cpp
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

  class SharedPtrRawPtrMisuseRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string message = to_lower_ascii(err.message);

      const bool mentionsSharedPtr =
          message.find("std::shared_ptr") != std::string::npos ||
          message.find("shared_ptr") != std::string::npos;

      if (!mentionsSharedPtr)
        return false;

      const bool rawPtrMisuse =
          message.find("constructed from raw pointer") != std::string::npos ||
          message.find("construction from raw pointer") != std::string::npos ||
          message.find("double delete") != std::string::npos ||
          message.find("double-delete") != std::string::npos ||
          message.find("double free") != std::string::npos ||
          message.find("will be deleted") != std::string::npos ||
          (message.find("may lead to") != std::string::npos &&
           message.find("delete") != std::string::npos) ||
          (message.find("raw pointer") != std::string::npos &&
           message.find("ownership") != std::string::npos);

      return rawPtrMisuse;
    }

    bool handle(
        const CompilerError &err,
        const ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: invalid std::shared_ptr ownership"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "create shared ownership with std::make_shared or pass an existing std::shared_ptr"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeSharedPtrRawPtrMisuseRule()
  {
    return std::make_unique<SharedPtrRawPtrMisuseRule>();
  }
} // namespace vix::cli::errors
