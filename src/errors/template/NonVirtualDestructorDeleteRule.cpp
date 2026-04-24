/**
 *
 *  @file NonVirtualDestructorDeleteRule.cpp
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

  class NonVirtualDestructorDeleteRule final : public ITemplateErrorRule
  {
  public:
    bool match(const vix::cli::errors::CompilerError &err) const override
    {
      const std::string &m = err.message;

      return (icontains(m, "delete") &&
              icontains(m, "non-virtual destructor")) ||
             (icontains(m, "deleting object of polymorphic class type") &&
              icontains(m, "non-virtual destructor")) ||
             (icontains(m, "destructor") &&
              icontains(m, "not virtual") &&
              icontains(m, "delete")) ||
             (icontains(m, "base class") &&
              icontains(m, "non-virtual destructor"));
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
                << "deleting through base class with non-virtual destructor"
                << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "the object is deleted through a base pointer, but the base class destructor is not virtual"
                << "\n";

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "make the base destructor virtual when the class is used polymorphically"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << fileName << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<ITemplateErrorRule> makeNonVirtualDestructorDeleteRule()
  {
    return std::make_unique<NonVirtualDestructorDeleteRule>();
  }
} // namespace vix::cli::errors::template_rules
