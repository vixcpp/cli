/**
 *
 *  @file RuntimeRuleUtils.cpp
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
#include <vix/cli/errors/runtime/RuntimeRuleUtils.hpp>
#include <vix/cli/errors/CodeFrame.hpp>
#include <vix/cli/errors/ErrorContext.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  namespace
  {
    bool is_digit(char c) noexcept
    {
      return std::isdigit(static_cast<unsigned char>(c)) != 0;
    }

    std::optional<int> parse_positive_int(
        const std::string &text,
        std::size_t begin,
        std::size_t *endOut = nullptr)
    {
      if (begin >= text.size() || !is_digit(text[begin]))
        return std::nullopt;

      int value = 0;
      std::size_t i = begin;

      while (i < text.size() && is_digit(text[i]))
      {
        value = (value * 10) + (text[i] - '0');
        ++i;
      }

      if (endOut)
        *endOut = i;

      return value;
    }

    RuntimeLocation parse_location_at(
        const std::string &text,
        std::size_t pathStart,
        std::size_t pathEnd)
    {
      RuntimeLocation location{};

      if (pathStart >= pathEnd || pathEnd >= text.size())
        return location;

      if (text[pathEnd] != ':')
        return location;

      std::size_t afterLine = 0;
      const auto parsedLine = parse_positive_int(text, pathEnd + 1, &afterLine);
      if (!parsedLine)
        return location;

      int column = 1;

      if (afterLine < text.size() && text[afterLine] == ':')
      {
        std::size_t afterColumn = 0;
        const auto parsedColumn =
            parse_positive_int(text, afterLine + 1, &afterColumn);

        if (parsedColumn)
          column = *parsedColumn;
      }

      location.file = std::filesystem::path(
          text.substr(pathStart, pathEnd - pathStart));
      location.line = *parsedLine;
      location.column = column;

      return location;
    }

    RuntimeLocation find_location_from_exact_source(
        const std::string &line,
        const std::filesystem::path &sourceFile)
    {
      if (sourceFile.empty())
        return {};

      const std::string source = sourceFile.string();
      const std::size_t pos = line.find(source);

      if (pos == std::string::npos)
        return {};

      return parse_location_at(line, pos, pos + source.size());
    }

    RuntimeLocation find_location_from_filename(
        const std::string &line,
        const std::filesystem::path &sourceFile)
    {
      if (sourceFile.empty())
        return {};

      const std::string filename = sourceFile.filename().string();
      if (filename.empty())
        return {};

      const std::size_t filenamePos = line.find(filename);
      if (filenamePos == std::string::npos)
        return {};

      const std::size_t filenameEnd = filenamePos + filename.size();
      if (filenameEnd >= line.size() || line[filenameEnd] != ':')
        return {};

      std::size_t pathStart = filenamePos;

      while (pathStart > 0)
      {
        const char c = line[pathStart - 1];

        if (std::isspace(static_cast<unsigned char>(c)) != 0 ||
            c == '(' ||
            c == '[')
        {
          break;
        }

        --pathStart;
      }

      return parse_location_at(line, pathStart, filenameEnd);
    }
  } // namespace

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

  std::string strip_line_comment(const std::string &line)
  {
    const std::size_t pos = line.find("//");
    if (pos == std::string::npos)
      return line;

    return line.substr(0, pos);
  }

  std::optional<std::vector<std::string>> read_file_lines(
      const std::filesystem::path &path)
  {
    std::ifstream ifs(path);
    if (!ifs)
      return std::nullopt;

    std::vector<std::string> lines;
    std::string line;

    while (std::getline(ifs, line))
      lines.push_back(line);

    return lines;
  }

  RuntimeLocation find_best_runtime_location(
      const std::string &log,
      const std::filesystem::path &sourceFile)
  {
    std::istringstream input(log);
    std::string line;

    while (std::getline(input, line))
    {
      if (line.find('#') == std::string::npos)
        continue;

      RuntimeLocation location =
          find_location_from_exact_source(line, sourceFile);

      if (location.valid())
        return location;

      location = find_location_from_filename(line, sourceFile);

      if (location.valid())
        return location;
    }

    return {};
  }

  RuntimeLocation find_best_runtime_location_or_source_hint(
      const std::string &log,
      const std::filesystem::path &sourceFile,
      const std::vector<std::string> &sourcePatterns)
  {
    RuntimeLocation location =
        find_best_runtime_location(log, sourceFile);

    if (location.valid())
      return location;

    if (sourceFile.empty() || sourcePatterns.empty())
      return {};

    const auto lines = read_file_lines(sourceFile);
    if (!lines)
      return {};

    for (std::size_t i = 0; i < lines->size(); ++i)
    {
      const std::string cleanLine =
          strip_line_comment((*lines)[i]);

      for (const auto &pattern : sourcePatterns)
      {
        if (pattern.empty())
          continue;

        const std::size_t pos = cleanLine.find(pattern);
        if (pos == std::string::npos)
          continue;

        RuntimeLocation hinted{};
        hinted.file = sourceFile;
        hinted.line = static_cast<int>(i + 1);
        hinted.column = static_cast<int>(pos + 1);

        return hinted;
      }
    }

    return {};
  }

  std::string make_at_text(
      const RuntimeLocation &location,
      const std::filesystem::path &sourceFile)
  {
    if (location.valid())
      return location.file.string() + ":" + std::to_string(location.line);

    if (!sourceFile.empty())
      return "source: " + sourceFile.filename().string();

    return {};
  }

  vix::cli::errors::CompilerError make_runtime_location(
      const std::filesystem::path &sourceFile,
      int line,
      int column,
      const std::string &message)
  {
    vix::cli::errors::CompilerError err;
    err.file = sourceFile.string();
    err.line = line;
    err.column = column;
    err.message = message;
    return err;
  }

  void print_runtime_codeframe(
      const vix::cli::errors::CompilerError &err)
  {
    ErrorContext ctx;
    CodeFrameOptions opt;
    opt.contextLines = 2;
    opt.maxLineWidth = 120;
    opt.tabWidth = 4;
    opt.leadingBlankLine = false;

    printCodeFrame(err, ctx, opt);
  }

  void print_runtime_hints_and_at(
      const std::vector<std::string> &hints,
      const std::string &at)
  {
    for (const auto &hintText : hints)
    {
      std::cerr << YELLOW
                << "hint: "
                << RESET
                << hintText
                << "\n";
    }

    if (!at.empty())
    {
      std::cerr << GREEN
                << "at: "
                << RESET
                << at
                << "\n";
    }
  }

  void print_runtime_log_excerpt(
      const std::string &log,
      std::size_t maxLines)
  {
    if (log.empty() || maxLines == 0)
      return;

    std::istringstream input(log);
    std::vector<std::string> lines;
    std::string line;

    while (std::getline(input, line))
      lines.push_back(line);

    if (lines.empty())
      return;

    std::size_t firstInteresting = lines.size();

    for (std::size_t i = 0; i < lines.size(); ++i)
    {
      const std::string &current = lines[i];

      const bool interesting =
          icontains(current, "terminate called") ||
          icontains(current, "terminate called without an active exception") ||
          icontains(current, "std::terminate") ||
          icontains(current, "std::thread") ||
          icontains(current, "thread::~thread") ||
          icontains(current, "~thread") ||
          icontains(current, "what():") ||
          icontains(current, "Aborted") ||
          icontains(current, "SIGABRT") ||
          icontains(current, "core dumped") ||
          icontains(current, "websocket") ||
          icontains(current, "LowLevelServer") ||
          icontains(current, "Session") ||
          icontains(current, "#");

      if (interesting)
      {
        firstInteresting = i;
        break;
      }
    }

    const std::size_t start =
        firstInteresting == lines.size()
            ? 0
            : (firstInteresting >= 2 ? firstInteresting - 2 : 0);

    const std::size_t end =
        std::min(lines.size(), start + maxLines);

    if (start >= end)
      return;

    std::cerr << "\n"
              << RED
              << "runtime log:"
              << RESET
              << "\n";

    for (std::size_t i = start; i < end; ++i)
    {
      if (lines[i].empty())
        continue;

      std::cerr << "  " << lines[i] << "\n";
    }

    std::cerr << "\n";
  }
} // namespace vix::cli::errors::runtime
