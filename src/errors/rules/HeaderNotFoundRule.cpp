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

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
  class HeaderNotFoundRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string &m = err.message;

      // Clang/GCC typical:
      //   "fatal error: 'x.hpp' file not found"
      //   "fatal error: x.hpp: No such file or directory"
      //
      // We support .hpp/.h/.hh/.hxx/.hpp and also "No such file" messages.
      const bool hasNotFound =
          (m.find("file not found") != std::string::npos) ||
          (m.find("No such file or directory") != std::string::npos);

      const bool looksLikeHeader =
          (m.find(".hpp") != std::string::npos) ||
          (m.find(".hh") != std::string::npos) ||
          (m.find(".hxx") != std::string::npos) ||
          (m.find(".h") != std::string::npos);

      return hasNotFound && looksLikeHeader;
    }

    bool handle(const CompilerError &err, const ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      const std::string fileName = filePath.filename().string();

      std::cerr << RED
                << "error: header file not found"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: check the include path and ensure the header exists"
                << RESET << "\n";

      std::cerr << GREEN
                << "at: " << fileName << ":" << err.line << ":" << err.column
                << RESET << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeHeaderNotFoundRule()
  {
    return std::make_unique<HeaderNotFoundRule>();
  }
} // namespace vix::cli::errors
