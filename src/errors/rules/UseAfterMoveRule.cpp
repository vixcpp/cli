/**
 *
 *  @file UseAfterMoveRUle.cpp
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

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <regex>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
  class UseAfterMoveRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string &msg = err.message;

      // Clang commonly:
      //   "use of 'x' after it was moved"
      //
      // Some variants can contain "use of moved value" etc.
      const bool afterMoved = (msg.find("after it was moved") != std::string::npos);
      const bool useOf = (msg.find("use of") != std::string::npos);

      const bool movedValue =
          (msg.find("use of moved") != std::string::npos) ||
          (msg.find("moved-from") != std::string::npos);

      return (afterMoved && useOf) || movedValue;
    }

    bool handle(const CompilerError &err, const ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      const std::string fileName = filePath.filename().string();

      std::string varName = "object";
      {
        std::regex re(R"(use of '([^']+)' after it was moved)");
        std::smatch m;
        if (std::regex_search(err.message, m, re) && m.size() >= 2)
          varName = m[1].str();
      }

      std::cerr << RED
                << "error: use-after-move"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: '" << varName << "' was moved; do not use it unless you reassign/reset it"
                << RESET << "\n";

      std::cerr << GREEN
                << "at: " << fileName << ":" << err.line << ":" << err.column
                << RESET << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeUseAfterMoveRule()
  {
    return std::make_unique<UseAfterMoveRule>();
  }
} // namespace vix::cli::errors
