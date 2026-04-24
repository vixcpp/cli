/**
 *
 *  @file LambdaCaptureLifetimeRule.cpp
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
#include <vix/cli/errors/template/ITemplateErrorRule.hpp>
#include <vix/cli/errors/CodeFrame.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::template_rules
{
  namespace
  {
    bool icontains(const std::string &text, const std::string &needle)
    {
      if (needle.empty())
        return true;

      auto lower = [](unsigned char c) -> char
      {
        if (c >= 'A' && c <= 'Z')
          return static_cast<char>(c + ('a' - 'A'));
        return static_cast<char>(c);
      };

      if (text.size() < needle.size())
        return false;

      for (std::size_t i = 0; i + needle.size() <= text.size(); ++i)
      {
        bool ok = true;

        for (std::size_t j = 0; j < needle.size(); ++j)
        {
          if (lower(static_cast<unsigned char>(text[i + j])) !=
              lower(static_cast<unsigned char>(needle[j])))
          {
            ok = false;
            break;
          }
        }

        if (ok)
          return true;
      }

      return false;
    }
  } // namespace

  class LambdaCaptureLifetimeRule final : public ITemplateErrorRule
  {
  public:
    bool match(const vix::cli::errors::CompilerError &err) const override
    {
      const std::string &m = err.message;

      return (icontains(m, "lambda capture") &&
              (icontains(m, "dangling") ||
               icontains(m, "lifetime") ||
               icontains(m, "reference to local"))) ||
             (icontains(m, "captures") &&
              icontains(m, "local variable") &&
              icontains(m, "returned")) ||
             (icontains(m, "address of stack memory") &&
              icontains(m, "returned")) ||
             (icontains(m, "reference to stack memory") &&
              icontains(m, "returned")) ||
             (icontains(m, "pointer to local variable") &&
              icontains(m, "returned"));
    }

    bool handle(
        const vix::cli::errors::CompilerError &err,
        const vix::cli::errors::ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      const std::string fileName = filePath.filename().string();

      std::cerr << RED
                << "error: "
                << RESET
                << "lambda capture may outlive captured local state"
                << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "this lambda likely captures a local object by reference and may outlive the scope where that object exists"
                << "\n";

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "capture by value when ownership is needed, or ensure the lambda does not escape the lifetime of the captured locals"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << fileName << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<ITemplateErrorRule> makeLambdaCaptureLifetimeRule()
  {
    return std::make_unique<LambdaCaptureLifetimeRule>();
  }
} // namespace vix::cli::errors::template_rules
