/**
 *
 *  @file MissingSemicolonRule.cpp
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
  class MissingSemicolonRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string &m = err.message;

      // GCC example:
      //   "expected ‘,’ or ‘;’ before ‘std’"
      //
      // Clang example:
      //   "expected ';' after expression"
      //
      // We match "expected" + a semicolon token / wording, avoiding too-specific patterns.
      const bool hasExpected = (m.find("expected") != std::string::npos);

      const bool hasSemicolonToken =
          (m.find("';'") != std::string::npos) ||                                      // common
          (m.find("‘;’") != std::string::npos) ||                                      // GCC fancy quotes
          (m.find(";") != std::string::npos && m.find("before") != std::string::npos); // fallback for "before" cases

      const bool mentionsSemicolonWord =
          (m.find("semicolon") != std::string::npos);

      return hasExpected && (hasSemicolonToken || mentionsSemicolonWord);
    }

    bool handle(const CompilerError &err, const ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      const std::string fileName = filePath.filename().string();

      std::cerr << RED
                << "error: missing ';'"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: add a semicolon at the end of the statement (often the previous line)"
                << RESET << "\n";

      std::cerr << GREEN
                << "at: " << fileName << ":" << err.line << ":" << err.column
                << RESET << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeMissingSemicolonRule()
  {
    return std::make_unique<MissingSemicolonRule>();
  }
} // namespace vix::cli::errors
