/**
 *
 *  @file SubstitutionFailureRule.cpp
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

  class SubstitutionFailureRule final : public ITemplateErrorRule
  {
  public:
    bool match(const vix::cli::errors::CompilerError &err) const override
    {
      const std::string &m = err.message;

      return icontains(m, "substitution failure") ||
             icontains(m, "substitution failed") ||
             icontains(m, "template argument deduction/substitution failed") ||
             icontains(m, "no matching function for call") ||
             icontains(m, "candidate template ignored") ||
             icontains(m, "requirements not satisfied");
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
                << "template substitution failed"
                << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "the compiler tried to instantiate a template with your types, but one or more required expressions or types were not valid"
                << "\n";

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "check the template constraints, required member types/functions, and whether the chosen type really supports the operations used inside the template"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << fileName << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<ITemplateErrorRule> makeSubstitutionFailureRule()
  {
    return std::make_unique<SubstitutionFailureRule>();
  }
} // namespace vix::cli::errors::template_rules
