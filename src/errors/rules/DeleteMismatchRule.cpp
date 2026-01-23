/**
 *
 *  @file DeleteMismatchRule.cpp
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
  class DeleteMismatchRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string &m = err.message;
      const bool hasMismatch =
          (m.find("mismatched delete") != std::string::npos) ||
          (m.find("mismatched-new-delete") != std::string::npos) ||
          (m.find("mismatched new/delete") != std::string::npos);

      const bool allocWithNewArray =
          (m.find("allocated with") != std::string::npos && m.find("new[]") != std::string::npos);

      const bool deleteArrayMention =
          (m.find("delete[]") != std::string::npos) ||
          (m.find("operator delete[]") != std::string::npos);

      const bool deleteMention =
          (m.find("delete") != std::string::npos) ||
          (m.find("operator delete") != std::string::npos);

      // Patterns like:
      // - allocated with new[] + delete
      // - allocated with new + delete[]
      const bool mismatchPair =
          (m.find("allocated with") != std::string::npos && m.find("new[]") != std::string::npos && deleteMention) ||
          (m.find("allocated with") != std::string::npos && m.find("new") != std::string::npos && deleteArrayMention);

      return hasMismatch || allocWithNewArray || mismatchPair;
    }

    bool handle(const CompilerError &err, const ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      const std::string fileName = filePath.filename().string();

      std::cerr << RED
                << "error: mismatched delete (delete vs delete[])"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: memory allocated with new[] must be freed with delete[]"
                << RESET << "\n";

      std::cerr << GREEN
                << "at: " << fileName << ":" << err.line << ":" << err.column
                << RESET << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeDeleteMismatchRule()
  {
    return std::make_unique<DeleteMismatchRule>();
  }
} // namespace vix::cli::errors
