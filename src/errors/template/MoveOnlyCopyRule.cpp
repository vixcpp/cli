/**
 *
 *  @file MoveOnlyCopyRule.cpp
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

    bool mentions_copy_semantic(const std::string &message)
    {
      return icontains(message, "copy constructor") ||
             icontains(message, "copy assignment") ||
             icontains(message, "operator=") ||
             icontains(message, "copy-constructor");
    }

    bool mentions_move_only_type(const std::string &message)
    {
      return icontains(message, "unique_ptr") ||
             icontains(message, "std::thread") ||
             icontains(message, "std::jthread") ||
             icontains(message, "std::promise") ||
             icontains(message, "std::future") ||
             icontains(message, "std::packaged_task") ||
             icontains(message, "non-copyable");
    }

    std::string choose_hint(const std::string &message)
    {
      (void)message;
      return "use std::move, pass by reference, or delete copying intentionally";
    }
  } // namespace

  class MoveOnlyCopyRule final : public ITemplateErrorRule
  {
  public:
    bool match(const vix::cli::errors::CompilerError &err) const override
    {
      const std::string &message = err.message;

      // Most reliable: deleted-function diagnostic mentioning a move-only type
      // and copy semantics.
      const bool deletedFn =
          icontains(message, "use of deleted function") ||
          icontains(message, "implicitly deleted") ||
          icontains(message, "is deleted");

      if (deletedFn &&
          (mentions_move_only_type(message) ||
           mentions_copy_semantic(message)))
      {
        return true;
      }

      if (icontains(message, "copy constructor is implicitly deleted"))
        return true;

      if (icontains(message, "non-copyable"))
        return true;

      return false;
    }

    bool handle(
        const vix::cli::errors::CompilerError &err,
        const vix::cli::errors::ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: copy of move-only type"
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

  std::unique_ptr<ITemplateErrorRule> makeMoveOnlyCopyRule()
  {
    return std::make_unique<MoveOnlyCopyRule>();
  }
} // namespace vix::cli::errors::template_rules
