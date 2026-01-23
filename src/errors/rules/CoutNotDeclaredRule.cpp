/**
 *
 *  @file CoutNotDeclaredRule.cpp
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
  class CoutNotDeclaredRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string &m = err.message;
      return (m.find("undeclared identifier") != std::string::npos && m.find("'cout'") != std::string::npos) ||
             (m.find("was not declared in this scope") != std::string::npos && m.find("'cout'") != std::string::npos);
    }

    bool handle(const CompilerError &err, const ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      std::string fileName = filePath.filename().string();

      std::cerr << RED << "error: cout is not declared" << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW << "hint: " << RESET
                << "include <iostream> and use std::cout" << "\n";

      std::cerr << GREEN << "at: " << RESET
                << fileName << ":" << err.line << ":" << err.column << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeCoutNotDeclaredRule()
  {
    return std::make_unique<CoutNotDeclaredRule>();
  }
} // namespace vix::cli::errors
