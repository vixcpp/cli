/**
 *
 *  @file NoMatchingOverloadWithConstraintsRule.cpp
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

  class NoMatchingOverloadWithConstraintsRule final : public ITemplateErrorRule
  {
  public:
    bool match(const vix::cli::errors::CompilerError &err) const override
    {
      const std::string &message = err.message;

      return (icontains(message, "no matching function for call") &&
              (icontains(message, "constraints not satisfied") ||
               icontains(message, "requirement") ||
               icontains(message, "concept"))) ||
             (icontains(message, "candidate function not viable") &&
              (icontains(message, "constraints not satisfied") ||
               icontains(message, "requirement"))) ||
             (icontains(message, "candidate template ignored") &&
              (icontains(message, "constraints not satisfied") ||
               icontains(message, "requirement"))) ||
             (icontains(message, "no matching overloaded function found") &&
              (icontains(message, "constraint") ||
               icontains(message, "concept")));
    }

    bool handle(
        const vix::cli::errors::CompilerError &err,
        const vix::cli::errors::ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: no overload satisfies the constraints"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "check argument types and the concepts or requires-clauses on the candidate overloads"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<ITemplateErrorRule> makeNoMatchingOverloadWithConstraintsRule()
  {
    return std::make_unique<NoMatchingOverloadWithConstraintsRule>();
  }
} // namespace vix::cli::errors::template_rules
