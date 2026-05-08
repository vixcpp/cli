/**
 *
 *  @file ConceptConstraintFailureRule.cpp
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
#include <optional>
#include <regex>
#include <string>
#include <string_view>

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

    bool is_system_path(std::string_view path) noexcept
    {
      return path.find("/usr/include/") != std::string_view::npos ||
             path.find("/usr/lib/") != std::string_view::npos ||
             path.find("/usr/local/include/") != std::string_view::npos ||
             path.find("/usr/local/lib/") != std::string_view::npos ||
             path.find("/lib/") != std::string_view::npos ||
             path.find("/lib64/") != std::string_view::npos;
    }

    bool is_user_path(std::string_view path) noexcept
    {
      if (path.empty())
        return false;

      if (is_system_path(path))
        return false;

      return path.find(".cpp") != std::string_view::npos ||
             path.find(".cc") != std::string_view::npos ||
             path.find(".cxx") != std::string_view::npos ||
             path.find(".hpp") != std::string_view::npos ||
             path.find(".hh") != std::string_view::npos ||
             path.find(".h") != std::string_view::npos;
    }

    vix::cli::errors::CompilerError make_location(
        const std::string &file,
        int line,
        int column,
        const std::string &message)
    {
      vix::cli::errors::CompilerError out;
      out.file = file;
      out.line = line;
      out.column = column > 0 ? column : 1;
      out.message = message;
      return out;
    }

    std::optional<vix::cli::errors::CompilerError>
    try_extract_first_user_location_from_log(
        const std::string &log,
        const std::string &message)
    {
      static const std::regex re(
          R"((/[^:\n]+?\.(?:c|cc|cpp|cxx|h|hpp|hh)):(\d+):(\d+))",
          std::regex::ECMAScript);

      std::smatch match;
      std::string::const_iterator it = log.begin();

      while (std::regex_search(it, log.cend(), match, re))
      {
        const std::string file = match[1].str();
        const int line = std::stoi(match[2].str());
        const int column = std::stoi(match[3].str());

        if (is_user_path(file))
          return make_location(file, line, column, message);

        it = match.suffix().first;
      }

      return std::nullopt;
    }

    const std::string &compiler_log_from_context(
        const vix::cli::errors::ErrorContext &ctx)
    {
      return ctx.buildLog;
    }

    vix::cli::errors::CompilerError best_location(
        const vix::cli::errors::CompilerError &err,
        const vix::cli::errors::ErrorContext &ctx)
    {
      const std::string &log = compiler_log_from_context(ctx);

      if (auto user_location =
              try_extract_first_user_location_from_log(log, err.message))
      {
        return *user_location;
      }

      if (is_user_path(err.file))
        return err;

      return err;
    }

    void print_hint_and_at(
        const vix::cli::errors::CompilerError &location,
        const vix::cli::errors::CompilerError &original)
    {
      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "check the type used at the call site; it does not provide the operation required by this concept"
                << "\n";

      if (location.file != original.file)
      {
        std::cerr << GRAY
                  << "note: original compiler location was "
                  << original.file << ":" << original.line << ":" << original.column
                  << RESET << "\n";
      }

      std::cerr << GREEN
                << "at: "
                << RESET
                << location.file << ":" << location.line << ":" << location.column
                << "\n";
    }
  } // namespace

  class ConceptConstraintFailureRule final : public ITemplateErrorRule
  {
  public:
    bool match(const vix::cli::errors::CompilerError &err) const override
    {
      const std::string &message = err.message;

      return icontains(message, "constraints not satisfied") ||
             icontains(message, "constraint failure") ||
             (icontains(message, "concept") &&
              icontains(message, "not satisfied")) ||
             icontains(message, "requirements not satisfied") ||
             icontains(message, "does not satisfy") ||
             icontains(message, "failed requirement");
    }

    bool handle(
        const vix::cli::errors::CompilerError &err,
        const vix::cli::errors::ErrorContext &ctx) const override
    {
      const auto location = best_location(err, ctx);

      std::cerr << RED
                << "error: concept constraint not satisfied"
                << RESET << "\n";

      CodeFrameOptions options;
      options.contextLines = 2;
      options.maxLineWidth = 120;
      options.tabWidth = 4;

      printCodeFrame(location, ctx, options);

      print_hint_and_at(location, err);

      return true;
    }
  };

  std::unique_ptr<ITemplateErrorRule> makeConceptConstraintFailureRule()
  {
    return std::make_unique<ConceptConstraintFailureRule>();
  }
} // namespace vix::cli::errors::template_rules
