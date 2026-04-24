/**
 *
 *  @file InvalidPromiseTypeRule.cpp
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

  class InvalidPromiseTypeRule final : public ITemplateErrorRule
  {
  public:
    bool match(const vix::cli::errors::CompilerError &err) const override
    {
      const std::string &m = err.message;

      return (icontains(m, "promise_type") &&
              icontains(m, "invalid")) ||
             (icontains(m, "promise_type") &&
              icontains(m, "not found")) ||
             (icontains(m, "no member named") &&
              icontains(m, "promise_type")) ||
             (icontains(m, "unable to find the promise type")) ||
             (icontains(m, "std::coroutine_traits") &&
              icontains(m, "promise_type")) ||
             (icontains(m, "promise type") &&
              icontains(m, "coroutine"));
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
                << "invalid coroutine promise type"
                << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "the coroutine return type does not expose a valid promise_type usable by the compiler"
                << "\n";

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "check the promise_type definition and ensure it provides the coroutine hooks required by the return type"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << fileName << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<ITemplateErrorRule> makeInvalidPromiseTypeRule()
  {
    return std::make_unique<InvalidPromiseTypeRule>();
  }
} // namespace vix::cli::errors::template_rules
