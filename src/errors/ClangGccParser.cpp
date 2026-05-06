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

namespace vix::cli::errors
{
  namespace
  {
    std::string trim_copy(std::string text)
    {
      while (!text.empty() &&
             (text.back() == '\n' ||
              text.back() == '\r' ||
              text.back() == ' ' ||
              text.back() == '\t'))
      {
        text.pop_back();
      }

      std::size_t start = 0;

      while (start < text.size() &&
             (text[start] == '\n' ||
              text[start] == '\r' ||
              text[start] == ' ' ||
              text[start] == '\t'))
      {
        ++start;
      }

      text.erase(0, start);
      return text;
    }

    std::string strip_ansi(std::string text)
    {
      static const std::regex ansiRe(
          R"(\x1B\[[0-9;?]*[ -/]*[@-~])");

      return std::regex_replace(text, ansiRe, "");
    }

    bool is_build_noise_line(const std::string &line)
    {
      return line.rfind("gmake", 0) == 0 ||
             line.rfind("make", 0) == 0 ||
             line.rfind("ninja", 0) == 0 ||
             line.rfind("[", 0) == 0;
    }

    bool is_error_severity(const std::string &severity)
    {
      return severity == "error" ||
             severity == "fatal error";
    }
  } // namespace

  std::vector<CompilerError> ClangGccParser::parse(
      const std::string &buildLog)
  {
    std::vector<CompilerError> out;

    // GCC / Clang examples:
    // /path/main.cpp:3:5: error: use of undeclared identifier 'std'
    // /path/foo.hpp:1:10: fatal error: 'h.hpp' file not found
    // /path/main.cpp:2:10: fatal error: huh.hpp: No such file or directory
    static const std::regex diagnosticRe(
        R"(^(.+?):([0-9]+):([0-9]+):\s*(fatal error|error|warning):\s*(.+)$)");

    std::istringstream input(buildLog);
    std::string line;

    while (std::getline(input, line))
    {
      line = strip_ansi(trim_copy(line));

      if (line.empty())
        continue;

      if (is_build_noise_line(line))
        continue;

      std::smatch match;

      if (!std::regex_search(line, match, diagnosticRe))
        continue;

      const std::string severity = match[4].str();

      if (!is_error_severity(severity))
        continue;

      CompilerError err;
      err.file = trim_copy(match[1].str());
      err.line = std::stoi(match[2].str());
      err.column = std::stoi(match[3].str());
      err.message = severity + ": " + trim_copy(match[5].str());
      err.raw = line;

      out.push_back(std::move(err));
    }

    return out;
  }
} // namespace vix::cli::errors
