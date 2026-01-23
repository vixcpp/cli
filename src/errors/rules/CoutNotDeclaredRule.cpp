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

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
  namespace
  {
    static std::string toLowerAscii(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c)
                     { return static_cast<char>(std::tolower(c)); });
      return s;
    }
  } // namespace

  class CoutNotDeclaredRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string m = toLowerAscii(err.message);

      const bool hasCout = (m.find("cout") != std::string::npos);
      if (!hasCout)
        return false;

      const bool undeclared =
          (m.find("undeclared identifier") != std::string::npos) ||
          (m.find("was not declared in this scope") != std::string::npos) ||
          (m.find("not declared in this scope") != std::string::npos);

      const bool notMemberStd =
          (m.find("not a member of") != std::string::npos) &&
          (m.find("std") != std::string::npos);

      return undeclared || notMemberStd;
    }

    bool handle(const CompilerError &err, const ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      std::string fileName = filePath.filename().string();

      std::cerr << RED << "error: std::cout is not available here" << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW << "hint: " << RESET
                << "add #include <iostream> (then use std::cout)" << "\n";

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
