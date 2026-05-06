/**
 *
 *  @file ConceptConstraintFailureRule.cpp
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

  class ConceptConstraintFailureRule final : public ITemplateErrorRule
  {
  public:
    bool match(const vix::cli::errors::CompilerError &err) const override
    {
      const std::string &message = err.message;

      return icontains(message, "constraints not satisfied") ||
             icontains(message, "constraint failure") ||
             (icontains(message, "concept") &&
              icontains(message, "not satisfied")) ||
             icontains(message, "requirements not satisfied") ||
             icontains(message, "does not satisfy") ||
             icontains(message, "failed requirement");
    }

    bool handle(
        const vix::cli::errors::CompilerError &err,
        const vix::cli::errors::ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: concept constraint not satisfied"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "check that the type provides the operations, nested types, and return types required by the concept"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<ITemplateErrorRule> makeConceptConstraintFailureRule()
  {
    return std::make_unique<ConceptConstraintFailureRule>();
  }
} // namespace vix::cli::errors::template_rules
