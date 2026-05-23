/**
 *
 *  @file PrivateConstructorRule.cpp
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

    std::string choose_hint(const std::string &message)
    {
      (void)message;
      return "use a public factory function or make the constructor public intentionally";
    }
  } // namespace

  class PrivateConstructorRule final : public ITemplateErrorRule
  {
  public:
    bool match(const vix::cli::errors::CompilerError &err) const override
    {
      const std::string &message = err.message;

      if (icontains(message, "constructor is private"))
        return true;

      if (icontains(message, "constructor is protected"))
        return true;

      if (icontains(message, "calling a private constructor"))
        return true;

      if (icontains(message, "is private within this context") &&
          (icontains(message, "constructor") ||
           icontains(message, "ctor") ||
           icontains(message, "()")))
      {
        return true;
      }

      return false;
    }

    bool handle(
        const vix::cli::errors::CompilerError &err,
        const vix::cli::errors::ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: constructor is inaccessible"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << choose_hint(err.message)
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<ITemplateErrorRule> makePrivateConstructorRule()
  {
    return std::make_unique<PrivateConstructorRule>();
  }
} // namespace vix::cli::errors::template_rules
