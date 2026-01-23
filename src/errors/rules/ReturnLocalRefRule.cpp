/**
 *
 *  @file ReturnLocalRefRule.cpp
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
  class ReturnLocalRefRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string &m = err.message;
      const bool mentionsReturn = (m.find("return") != std::string::npos) || (m.find("returned") != std::string::npos);
      const bool localAddr =
          (m.find("address of local") != std::string::npos) ||
          (m.find("local variable") != std::string::npos && m.find("returned") != std::string::npos);
      const bool stackRef =
          (m.find("reference to stack") != std::string::npos) ||
          (m.find("stack memory") != std::string::npos);
      return mentionsReturn && (localAddr || stackRef);
    }

    bool handle(const CompilerError &err, const ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      const std::string fileName = filePath.filename().string();

      std::cerr << RED
                << "error: returning reference or pointer to a local object"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: return by value or ensure the referenced object outlives the function"
                << RESET << "\n";

      std::cerr << GREEN
                << "at: " << fileName << ":" << err.line << ":" << err.column
                << RESET << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeReturnLocalRefRule()
  {
    return std::make_unique<ReturnLocalRefRule>();
  }
} // namespace vix::cli::errors
