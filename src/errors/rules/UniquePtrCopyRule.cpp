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

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
  class UniquePtrCopyRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string &m = err.message;

      // Clang/GCC patterns:
      // - "use of deleted function ... std::unique_ptr"
      // - "call to deleted constructor ... std::unique_ptr"
      // - "attempt to use a deleted function"
      const bool deletedFn =
          (m.find("use of deleted function") != std::string::npos) ||
          (m.find("call to deleted constructor") != std::string::npos) ||
          (m.find("attempt to use a deleted function") != std::string::npos) ||
          (m.find("is deleted") != std::string::npos);

      const bool mentionsUnique =
          (m.find("std::unique_ptr") != std::string::npos) ||
          (m.find("unique_ptr") != std::string::npos);

      return deletedFn && mentionsUnique;
    }

    bool handle(const CompilerError &err, const ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      const std::string fileName = filePath.filename().string();

      std::cerr << RED
                << "error: std::unique_ptr cannot be copied"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: move the std::unique_ptr or pass it by reference"
                << RESET << "\n";

      std::cerr << GREEN
                << "at: " << fileName << ":" << err.line << ":" << err.column
                << RESET << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeUniquePtrCopyRule()
  {
    return std::make_unique<UniquePtrCopyRule>();
  }
} // namespace vix::cli::errors
