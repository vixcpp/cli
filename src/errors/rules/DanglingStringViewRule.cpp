/**
 *
 *  @file DanglingStringViewRule.cpp
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
  class DanglingStringViewRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string &m = err.message;
      const bool hasDangling = (m.find("dangling") != std::string::npos);
      const bool hasView =
          (m.find("string_view") != std::string::npos) ||
          (m.find("std::basic_string_view") != std::string::npos);
      const bool hasRef = (m.find("reference") != std::string::npos);
      return hasDangling && (hasView || hasRef);
    }

    bool handle(const CompilerError &err, const ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      const std::string fileName = filePath.filename().string();

      std::cerr << RED
                << "error: dangling std::string_view"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: std::string_view must refer to data that outlives it"
                << RESET << "\n";

      std::cerr << GREEN
                << "at: " << fileName << ":" << err.line << ":" << err.column
                << RESET << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeDanglingStringViewRule()
  {
    return std::make_unique<DanglingStringViewRule>();
  }
} // namespace vix::cli::errors
