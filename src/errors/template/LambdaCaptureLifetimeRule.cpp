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
      const std::string &message = err.message;

      return (icontains(message, "lambda capture") &&
              (icontains(message, "dangling") ||
               icontains(message, "lifetime") ||
               icontains(message, "reference to local"))) ||
             (icontains(message, "captures") &&
              icontains(message, "local variable") &&
              icontains(message, "returned")) ||
             (icontains(message, "address of stack memory") &&
              icontains(message, "returned")) ||
             (icontains(message, "reference to stack memory") &&
              icontains(message, "returned")) ||
             (icontains(message, "pointer to local variable") &&
              icontains(message, "returned"));
    }

    bool handle(
        const vix::cli::errors::CompilerError &err,
        const vix::cli::errors::ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: unsafe lambda capture"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "capture by value or ensure the lambda does not outlive the captured local variables"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<ITemplateErrorRule> makeLambdaCaptureLifetimeRule()
  {
    return std::make_unique<LambdaCaptureLifetimeRule>();
  }
} // namespace vix::cli::errors::template_rules
