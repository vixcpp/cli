/**
 *
 *  @file DanglingStringViewRule.cpp
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
#include <vix/cli/errors/IErrorRule.hpp>
#include <vix/cli/errors/CodeFrame.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
  namespace
  {
    std::string to_lower_ascii(std::string text)
    {
      std::transform(
          text.begin(),
          text.end(),
          text.begin(),
          [](unsigned char c)
          {
            return static_cast<char>(std::tolower(c));
          });

      return text;
    }

    bool read_all_lines(
        const std::string &file,
        std::vector<std::string> &out)
    {
      std::ifstream in(file);
      if (!in.is_open())
        return false;

      std::string line;
      while (std::getline(in, line))
        out.push_back(line);

      return true;
    }

    bool file_mentions_string_view_near_line(
        const std::string &file,
        int line,
        int before = 25,
        int after = 8)
    {
      if (file.empty() || line <= 0)
        return false;

      std::vector<std::string> lines;
      if (!read_all_lines(file, lines))
        return false;

      const int lineCount = static_cast<int>(lines.size());
      if (lineCount <= 0)
        return false;

      const int from = std::max(1, line - before);
      const int to = std::min(lineCount, line + after);

      for (int currentLine = from; currentLine <= to; ++currentLine)
      {
        const std::string text =
            to_lower_ascii(lines[static_cast<std::size_t>(currentLine - 1)]);

        if (text.find("string_view") != std::string::npos ||
            text.find("basic_string_view") != std::string::npos)
        {
          return true;
        }
      }

      return false;
    }
  } // namespace

  class DanglingStringViewRule final : public IErrorRule
  {
  public:
    bool match(const CompilerError &err) const override
    {
      const std::string message = to_lower_ascii(err.message);

      const bool hasLifetimeClue =
          message.find("returning reference to temporary") != std::string::npos ||
          message.find("returning address of local") != std::string::npos ||
          message.find("address of local variable") != std::string::npos ||
          (message.find("local variable") != std::string::npos &&
           message.find("returned") != std::string::npos) ||
          message.find("does not live long enough") != std::string::npos ||
          message.find("dangling") != std::string::npos;

      if (!hasLifetimeClue)
        return false;

      return file_mentions_string_view_near_line(err.file, err.line);
    }

    bool handle(
        const CompilerError &err,
        const ErrorContext &ctx) const override
    {
      std::cerr << RED
                << "error: dangling std::string_view"
                << RESET << "\n";

      printCodeFrame(err, ctx);

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << "return std::string when ownership is needed or ensure the viewed storage outlives the std::string_view"
                << "\n";

      std::cerr << GREEN
                << "at: "
                << RESET
                << err.file << ":" << err.line << ":" << err.column
                << "\n";

      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeDanglingStringViewRule()
  {
    return std::make_unique<DanglingStringViewRule>();
  }
} // namespace vix::cli::errors
