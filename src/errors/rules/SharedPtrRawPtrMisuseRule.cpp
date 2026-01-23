/**
 *
 *  @file SharedPtrRawPtrMisuseRule.cpp
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
  class SharedPtrRawPtrMisuseRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string &m = err.message;

      const bool mentionsShared =
          (m.find("std::shared_ptr") != std::string::npos) ||
          (m.find("shared_ptr") != std::string::npos);

      // Match known warning/error phrases across toolchains/lints
      const bool rawPtrMisuse =
          (m.find("constructed from raw pointer") != std::string::npos) ||
          (m.find("construction from raw pointer") != std::string::npos) ||
          (m.find("double delete") != std::string::npos) ||
          (m.find("double-delete") != std::string::npos) ||
          (m.find("may lead to") != std::string::npos && m.find("delete") != std::string::npos) ||
          (m.find("will be deleted") != std::string::npos && m.find("shared_ptr") != std::string::npos);

      return mentionsShared && rawPtrMisuse;
    }

    bool handle(const CompilerError &err, const ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      const std::string fileName = filePath.filename().string();

      std::cerr << RED
                << "error: invalid std::shared_ptr ownership"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: never create multiple std::shared_ptr from the same raw pointer"
                << RESET << "\n";

      std::cerr << GREEN
                << "at: " << fileName << ":" << err.line << ":" << err.column
                << RESET << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeSharedPtrRawPtrMisuseRule()
  {
    return std::make_unique<SharedPtrRawPtrMisuseRule>();
  }
} // namespace vix::cli::errors
