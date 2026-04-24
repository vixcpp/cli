/**
 *
 *  @file CoroutineReturnTypeRule.cpp
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

  class CoroutineReturnTypeRule final : public ITemplateErrorRule
  {
  public:
    bool match(const vix::cli::errors::CompilerError &err) const override
    {
      const std::string &m = err.message;

      return (icontains(m, "coroutine") &&
              icontains(m, "return type")) ||
             (icontains(m, "unable to find the promise type")) ||
             (icontains(m, "this function cannot be a coroutine")) ||
             (icontains(m, "std::coroutine_traits") &&
              icontains(m, "promise_type")) ||
             (icontains(m, "no member named") &&
              icontains(m, "promise_type")) ||
             (icontains(m, "coroutine") &&
              icontains(m, "promise_type"));
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
                << "invalid coroutine return type"
                << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "the return type used by this coroutine does not provide the coroutine interface expected by the compiler"
                << "\n";

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "check that the return type exposes a valid promise_type and that it is designed to be used as a coroutine return type"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << fileName << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<ITemplateErrorRule> makeCoroutineReturnTypeRule()
  {
    return std::make_unique<CoroutineReturnTypeRule>();
  }
} // namespace vix::cli::errors::template_rules
