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

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
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
} // namespace vix::cli::errors::runtime
