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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
  namespace
  {
    static std::string toLowerAscii(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c)
                     { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    static bool readAllLines(const std::string &file, std::vector<std::string> &out)
    {
      std::ifstream in(file);
      if (!in.is_open())
        return false;

      std::string line;
      while (std::getline(in, line))
        out.push_back(line);

      return true;
    }

    static bool fileMentionsStringViewNearLine(
        const std::string &file,
        int line1Based,
        int before = 25,
        int after = 8)
    {
      if (file.empty() || line1Based <= 0)
        return false;

      std::vector<std::string> lines;
      if (!readAllLines(file, lines))
        return false;

      const int n = static_cast<int>(lines.size());
      if (n <= 0)
        return false;

      int from = std::max(1, line1Based - before);
      int to = std::min(n, line1Based + after);

      for (int ln = from; ln <= to; ++ln)
      {
        const std::string s = toLowerAscii(lines[static_cast<std::size_t>(ln - 1)]);
        if (s.find("string_view") != std::string::npos ||
            s.find("basic_string_view") != std::string::npos)
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
      const std::string m = toLowerAscii(err.message);

      const bool lifetimeClue =
          (m.find("returning reference to temporary") != std::string::npos) ||
          (m.find("returning address of local") != std::string::npos) ||
          (m.find("address of local variable") != std::string::npos) ||
          (m.find("local variable") != std::string::npos && m.find("returned") != std::string::npos) ||
          (m.find("does not live long enough") != std::string::npos) ||
          (m.find("dangling") != std::string::npos);

      if (!lifetimeClue)
        return false;

      return fileMentionsStringViewNearLine(err.file, err.line);
    }

    bool handle(const CompilerError &err, const ErrorContext &ctx) const override
    {
      std::filesystem::path filePath(err.file);
      const std::string fileName = filePath.filename().string();

      std::cerr << RED << "runtime error: use-after-return" << RESET << "\n\n";
      printCodeFrame(err, ctx);
      std::cerr << YELLOW << "hint: " << RESET
                << "a pointer/reference/view escaped a function and outlived its stack variable"
                << "\n";
      std::cerr << GREEN << "at: " << RESET
                << err.file << ":" << err.line
                << "\n";
      return true;
    }
  };

  std::unique_ptr<IErrorRule> makeDanglingStringViewRule()
  {
    return std::make_unique<DanglingStringViewRule>();
  }
} // namespace vix::cli::errors
