/**
 *
 *  @file ProcessNullptrAmbiguityRule.cpp
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
  class ProcessNullptrAmbiguityRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string &m = err.message;

      // Clang/GCC usually:
      //   "no matching function for call to 'process'"
      // Sometimes:
      //   "call to 'process' is ambiguous"
      //   "ambiguous call to ... process"
      const bool noMatching = (m.find("no matching function for call") != std::string::npos) &&
                              (m.find("process") != std::string::npos);

      const bool ambiguous = (m.find("ambiguous") != std::string::npos) &&
                             (m.find("process") != std::string::npos);

      return noMatching || ambiguous;
    }

    bool handle(const CompilerError &err, const ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      const std::string fileName = filePath.filename().string();

      std::cerr << RED
                << "error: ambiguous call to function"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: disambiguate the call with an explicit cast or exact type"
                << RESET << "\n";

      std::cerr << GREEN
                << "at: " << fileName << ":" << err.line << ":" << err.column
                << RESET << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeProcessNullptrAmbiguityRule()
  {
    return std::make_unique<ProcessNullptrAmbiguityRule>();
  }
} // namespace vix::cli::errors
