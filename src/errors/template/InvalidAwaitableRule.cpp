/**
 *
 *  @file InvalidAwaitableRule.cpp
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

  class InvalidAwaitableRule final : public ITemplateErrorRule
  {
  public:
    bool match(const vix::cli::errors::CompilerError &err) const override
    {
      const std::string &m = err.message;

      return (icontains(m, "co_await") &&
              icontains(m, "awaitable")) ||
             (icontains(m, "co_await") &&
              icontains(m, "cannot be used")) ||
             (icontains(m, "no member named") &&
              (icontains(m, "await_ready") ||
               icontains(m, "await_suspend") ||
               icontains(m, "await_resume"))) ||
             (icontains(m, "no matching") &&
              icontains(m, "operator co_await")) ||
             (icontains(m, "not awaitable")) ||
             (icontains(m, "awaiter") &&
              icontains(m, "invalid"));
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
                << "invalid awaitable used with co_await"
                << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "the object used with co_await does not satisfy the awaitable protocol expected by the compiler"
                << "\n";

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "check operator co_await, await_ready(), await_suspend(), and await_resume() on the awaited type"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << fileName << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<ITemplateErrorRule> makeInvalidAwaitableRule()
  {
    return std::make_unique<InvalidAwaitableRule>();
  }
} // namespace vix::cli::errors::template_rules
