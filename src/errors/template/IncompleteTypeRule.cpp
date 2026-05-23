/**
 *
 *  @file IncompleteTypeRule.cpp
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
      return "include the full definition of the type before using its size, "
             "members, or deleting it";
    }
  } // namespace

  class IncompleteTypeRule final : public ITemplateErrorRule
  {
  public:
    bool match(const vix::cli::errors::CompilerError &err) const override
    {
      const std::string &message = err.message;

      if (icontains(message, "invalid use of incomplete type"))
        return true;

      if (icontains(message, "incomplete type"))
        return true;

      if (icontains(message, "sizeof incomplete type"))
        return true;

      // "forward declaration of" alone is a note, not an error;
      // require it to come with another diagnostic signal.
      if (icontains(message, "forward declaration of") &&
          (icontains(message, "incomplete") ||
           icontains(message, "invalid use") ||
           icontains(message, "sizeof")))
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
                << "error: incomplete type used"
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

  std::unique_ptr<ITemplateErrorRule> makeIncompleteTypeRule()
  {
    return std::make_unique<IncompleteTypeRule>();
  }
} // namespace vix::cli::errors::template_rules
