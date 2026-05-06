/**
 *
 *  @file UncaughtExceptionRule.cpp
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
#include <vix/cli/errors/rules/UncaughtExceptionRule.hpp>

#include <vix/cli/errors/CodeFrame.hpp>
#include <vix/cli/errors/CompilerError.hpp>
#include <vix/cli/errors/ErrorContext.hpp>

#include <vix/cli/Style.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <string>
#include <string_view>

using namespace vix::cli::style;

namespace vix::cli::errors::rules
{
  namespace
  {
    char to_lower_ascii(unsigned char c) noexcept
    {
      return static_cast<char>(std::tolower(c));
    }

    bool icontains(std::string_view haystack, std::string_view needle) noexcept
    {
      if (needle.empty())
        return true;

      if (haystack.size() < needle.size())
        return false;

      for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i)
      {
        bool ok = true;

        for (std::size_t j = 0; j < needle.size(); ++j)
        {
          const unsigned char a = static_cast<unsigned char>(haystack[i + j]);
          const unsigned char b = static_cast<unsigned char>(needle[j]);

          if (to_lower_ascii(a) != to_lower_ascii(b))
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

    bool contains_any(
        std::string_view text,
        const std::initializer_list<std::string_view> &needles) noexcept
    {
      for (const auto needle : needles)
      {
        if (!needle.empty() && icontains(text, needle))
          return true;
      }

      return false;
    }

    std::string_view trim_view(std::string_view text) noexcept
    {
      while (!text.empty() &&
             std::isspace(static_cast<unsigned char>(text.front())) != 0)
      {
        text.remove_prefix(1);
      }

      while (!text.empty() &&
             std::isspace(static_cast<unsigned char>(text.back())) != 0)
      {
        text.remove_suffix(1);
      }

      return text;
    }

    std::optional<std::size_t> find_line_by_substring(
        const std::filesystem::path &file,
        std::string_view needle)
    {
      if (file.empty() || needle.empty())
        return std::nullopt;

      std::ifstream in(file);
      if (!in)
        return std::nullopt;

      std::string line;
      std::size_t lineNumber = 0;

      while (std::getline(in, line))
      {
        ++lineNumber;

        if (line.find(std::string(needle)) != std::string::npos)
          return lineNumber;
      }

      return std::nullopt;
    }

    std::optional<std::size_t> guess_throw_location(
        const std::filesystem::path &sourceFile,
        const std::string &whatMessage)
    {
      if (sourceFile.empty())
        return std::nullopt;

      if (!whatMessage.empty())
      {
        if (auto line = find_line_by_substring(sourceFile, whatMessage))
          return line;

        const std::string quoted = "\"" + whatMessage + "\"";
        if (auto line = find_line_by_substring(sourceFile, quoted))
          return line;
      }

      if (auto line = find_line_by_substring(sourceFile, "throw "))
        return line;

      return std::nullopt;
    }

    std::string extract_exception_type(const std::string &log)
    {
      {
        static const std::regex re(R"(throwing an instance of '([^']+)')");
        std::smatch match;

        if (std::regex_search(log, match, re) && match.size() >= 2)
          return match[1].str();
      }

      {
        static const std::regex re(R"(uncaught exception of type ([^:\n]+))");
        std::smatch match;

        if (std::regex_search(log, match, re) && match.size() >= 2)
          return std::string(trim_view(std::string_view(match[1].str())));
      }

      return {};
    }

    std::string extract_what_message(const std::string &log)
    {
      static const std::regex re(R"(what\(\):\s*([^\n\r]+))");
      std::smatch match;

      if (std::regex_search(log, match, re) && match.size() >= 2)
        return std::string(trim_view(std::string_view(match[1].str())));

      return {};
    }

    std::string make_hint(
        const std::string &exceptionType,
        const std::string &whatMessage)
    {
      if (!whatMessage.empty())
        return "catch the exception in main() or handle the failing operation before it escapes";

      if (!exceptionType.empty())
        return "catch " + exceptionType + " in main() or handle it before it escapes";

      return "catch exceptions in main() or handle them before they escape";
    }

    std::string make_at_text(
        const std::filesystem::path &sourceFile,
        int line)
    {
      if (sourceFile.empty())
        return {};

      if (line > 0)
        return sourceFile.string() + ":" + std::to_string(line);

      return "source: " + sourceFile.filename().string();
    }
  } // namespace

  bool handleUncaughtException(
      const std::string &runtimeLog,
      const std::filesystem::path &sourceFile)
  {
    const bool hit = contains_any(
        runtimeLog,
        {
            "terminate called after throwing an instance of",
            "terminating with uncaught exception",
            "libc++abi: terminating with uncaught exception",
            "std::terminate",
        });

    if (!hit)
      return false;

    const std::string exceptionType = extract_exception_type(runtimeLog);
    const std::string whatMessage = extract_what_message(runtimeLog);

    std::cerr << RED
              << "runtime error: uncaught exception"
              << RESET << "\n";

    if (!exceptionType.empty() && !whatMessage.empty())
    {
      std::cerr << GRAY
                << exceptionType << ": "
                << RESET
                << whatMessage
                << "\n";
    }
    else if (!exceptionType.empty())
    {
      std::cerr << GRAY
                << "type: "
                << RESET
                << exceptionType
                << "\n";
    }
    else if (!whatMessage.empty())
    {
      std::cerr << GRAY
                << "what: "
                << RESET
                << whatMessage
                << "\n";
    }

    int line = 0;

    if (auto guessedLine = guess_throw_location(sourceFile, whatMessage))
    {
      line = static_cast<int>(*guessedLine);

      CompilerError loc;
      std::error_code ec;

      loc.file = std::filesystem::weakly_canonical(sourceFile, ec).string();
      if (ec)
        loc.file = sourceFile.string();

      loc.line = line;
      loc.column = 1;
      loc.message = "uncaught exception";

      ErrorContext ctx;
      CodeFrameOptions opt;
      opt.contextLines = 2;
      opt.maxLineWidth = 120;
      opt.tabWidth = 4;

      printCodeFrame(loc, ctx, opt);
    }

    std::cerr << YELLOW
              << "hint: "
              << RESET
              << make_hint(exceptionType, whatMessage)
              << "\n";

    const std::string at = make_at_text(sourceFile, line);
    if (!at.empty())
    {
      std::cerr << GREEN
                << "at: "
                << RESET
                << at
                << "\n";
    }

    return true;
  }

} // namespace vix::cli::errors::rules
