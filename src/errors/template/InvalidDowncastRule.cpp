/**
 *
 *  @file InvalidDowncastRule.cpp
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

  class InvalidDowncastRule final : public ITemplateErrorRule
  {
  public:
    bool match(const vix::cli::errors::CompilerError &err) const override
    {
      const std::string &m = err.message;

      return (icontains(m, "dynamic_cast") &&
              icontains(m, "not polymorphic")) ||
             (icontains(m, "invalid static_cast") &&
              icontains(m, "base") &&
              icontains(m, "derived")) ||
             (icontains(m, "cannot dynamic_cast")) ||
             (icontains(m, "downcast") &&
              icontains(m, "invalid")) ||
             (icontains(m, "source type is not polymorphic")) ||
             (icontains(m, "cannot cast") &&
              icontains(m, "base") &&
              icontains(m, "derived"));
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
                << "invalid downcast"
                << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "this cast tries to convert a base object or pointer into a derived type in a way that is not safe or not allowed"
                << "\n";

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "use dynamic_cast with a polymorphic base when runtime checking is needed, or redesign the ownership and type flow to avoid unsafe downcasts"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << fileName << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<ITemplateErrorRule> makeInvalidDowncastRule()
  {
    return std::make_unique<InvalidDowncastRule>();
  }
} // namespace vix::cli::errors::template_rules
