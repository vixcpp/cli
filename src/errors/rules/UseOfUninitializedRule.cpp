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
#include "vix/cli/errors/IErrorRule.hpp"
#include "vix/cli/errors/CodeFrame.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
  class UseOfUninitializedRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string &m = err.message;

      // Typical diagnostics:
      // - "may be used uninitialized" (GCC)
      // - "is used uninitialized" (some builds)
      // - "use of uninitialized" (clang)
      // - "uninitialized use" (varies)
      const bool mentionsUninit = (m.find("uninitialized") != std::string::npos);

      const bool strongPhrase =
          (m.find("may be used") != std::string::npos) ||
          (m.find("is used") != std::string::npos) ||
          (m.find("use of uninitialized") != std::string::npos) ||
          (m.find("uninitialized use") != std::string::npos);

      return mentionsUninit && strongPhrase;
    }

    bool handle(const CompilerError &err, const ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      const std::string fileName = filePath.filename().string();

      std::cerr << RED
                << "error: use of an uninitialized value"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: initialize the variable before using it"
                << RESET << "\n";

      std::cerr << GREEN
                << "at: " << fileName << ":" << err.line << ":" << err.column
                << RESET << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeUseOfUninitializedRule()
  {
    return std::make_unique<UseOfUninitializedRule>();
  }
} // namespace vix::cli::errors
