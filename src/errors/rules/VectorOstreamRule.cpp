/**
 *
 *  @file UseOfUninitializedRule.cpp
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
  class VectorOstreamRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string &msg = err.message;

      // GCC:
      //   "no match for ‘operator<<’ (operand types are ‘std::ostream’ ... and ‘std::vector<int>’)"
      //
      // Clang:
      //   "invalid operands to binary expression ('std::ostream' and 'std::vector<int>')"

      const bool hasVector = (msg.find("std::vector") != std::string::npos);

      // Prefer insertion detection:
      // - GCC literally mentions operator<<
      // - Clang may not always mention operator<<, but "invalid operands" is strong
      const bool mentionsOperator =
          (msg.find("operator<<") != std::string::npos) ||
          (msg.find("operator <<") != std::string::npos);

      const bool gccNoMatch =
          (msg.find("no match for") != std::string::npos) ||
          (msg.find("no matching") != std::string::npos);

      const bool clangInvalidOperands =
          (msg.find("invalid operands") != std::string::npos) ||
          (msg.find("invalid operands to binary expression") != std::string::npos);

      // Optional extra signal (not required to avoid false negatives):
      const bool hasStream =
          (msg.find("std::ostream") != std::string::npos) ||
          (msg.find("basic_ostream") != std::string::npos);

      // IMPORTANT:
      // Avoid matching on raw "<<" because it can appear in many unrelated messages.
      // I only use explicit operator<< OR clangInvalidOperands/gccNoMatch signals.
      return hasVector && (gccNoMatch || clangInvalidOperands) && (mentionsOperator || hasStream);
    }

    bool handle(const CompilerError &err, const ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      const std::string fileName = filePath.filename().string();

      std::cerr << RED
                << "error: no operator<< for std::vector"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: print the elements manually or define an operator<< overload"
                << RESET << "\n";

      std::cerr << GREEN
                << "at: " << fileName << ":" << err.line << ":" << err.column
                << RESET << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeVectorOstreamRule()
  {
    return std::make_unique<VectorOstreamRule>();
  }
} // namespace vix::cli::errors
