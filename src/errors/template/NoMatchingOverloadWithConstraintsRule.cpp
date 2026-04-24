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

  class NoMatchingOverloadWithConstraintsRule final : public ITemplateErrorRule
  {
  public:
    bool match(const vix::cli::errors::CompilerError &err) const override
    {
      const std::string &m = err.message;

      return (icontains(m, "no matching function for call") &&
              (icontains(m, "constraints not satisfied") ||
               icontains(m, "requirement") ||
               icontains(m, "concept"))) ||
             (icontains(m, "candidate function not viable") &&
              (icontains(m, "constraints not satisfied") ||
               icontains(m, "requirement"))) ||
             (icontains(m, "candidate template ignored") &&
              (icontains(m, "constraints not satisfied") ||
               icontains(m, "requirement"))) ||
             (icontains(m, "no matching overloaded function found") &&
              (icontains(m, "constraint") || icontains(m, "concept")));
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
                << "no matching overload satisfies the constraints"
                << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "the call does not match any overload whose concepts or requires-clauses are satisfied by the provided arguments"
                << "\n";

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "check argument types, required operations, and whether the chosen overload is filtered out by constraints"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << fileName << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<ITemplateErrorRule> makeNoMatchingOverloadWithConstraintsRule()
  {
    return std::make_unique<NoMatchingOverloadWithConstraintsRule>();
  }
} // namespace vix::cli::errors::template_rules
