/**
 *
 *  @file ClangGccParser.cpp
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
#include <vix/cli/errors/ClangGccParser.hpp>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <regex>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <sstream>
#include <string>
#include <algorithm>
#include <cctype>

namespace vix::cli::errors
{
  namespace
  {
    std::string trim_copy(std::string s)
    {
      while (!s.empty() &&
             (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
      {
        s.pop_back();
      }

      std::size_t i = 0;
      while (i < s.size() &&
             (s[i] == '\n' || s[i] == '\r' || s[i] == ' ' || s[i] == '\t'))
      {
        ++i;
      }

      s.erase(0, i);
      return s;
    }

    std::string strip_ansi(std::string s)
    {
      static const std::regex ansiRe(R"(\x1B\[[0-9;?]*[ -/]*[@-~])");
      return std::regex_replace(s, ansiRe, "");
    }
  }

  std::vector<CompilerError> ClangGccParser::parse(const std::string &buildLog)
  {
    std::vector<CompilerError> out;

    // GCC / Clang examples:
    // /path/main.cpp:3:5: error: use of undeclared identifier 'std'
    // /path/foo.hpp:1:10: fatal error: 'h.hpp' file not found
    // /path/main.cpp:2:10: fatal error: huh.hpp: No such file or directory
    static const std::regex re(
        R"(^(.+?):([0-9]+):([0-9]+):\s*(fatal error|error|warning):\s*(.+)$)");

    std::istringstream iss(buildLog);
    std::string line;

    while (std::getline(iss, line))
    {
      line = strip_ansi(trim_copy(line));

      if (line.empty())
        continue;

      if (line.rfind("gmake", 0) == 0 ||
          line.rfind("make", 0) == 0 ||
          line.rfind("ninja", 0) == 0)
      {
        continue;
      }

      std::smatch m;
      if (std::regex_search(line, m, re))
      {
        CompilerError e;
        e.file = trim_copy(m[1].str());
        e.line = std::stoi(m[2].str());
        e.column = std::stoi(m[3].str());

        const std::string severity = m[4].str();
        const std::string message = trim_copy(m[5].str());

        e.message = severity + ": " + message;
        e.raw = line;

        if (severity == "error" || severity == "fatal error")
          out.push_back(std::move(e));
      }
    }

    return out;
  }
} // namespace vix::cli::errors
