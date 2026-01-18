/**
 *
 *  @file Strings.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_STRINGS_HPP
#define VIX_CLI_STRINGS_HPP

#include <string>
#include <string_view>
#include <vector>

namespace vix::cli::util
{
  inline bool is_ws(unsigned char c) noexcept
  {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }

  // O(n) trim without repeated erase(begin())
  inline std::string trim(std::string s)
  {
    if (s.empty())
      return s;

    std::size_t begin = 0;
    while (begin < s.size() && is_ws(static_cast<unsigned char>(s[begin])))
      ++begin;

    std::size_t end = s.size();
    while (end > begin && is_ws(static_cast<unsigned char>(s[end - 1])))
      --end;

    if (begin == 0 && end == s.size())
      return s;

    return s.substr(begin, end - begin);
  }

  // Formats milliseconds as "Xs" with one decimal, without iostream.
  inline std::string format_seconds(long long ms)
  {
    if (ms < 0)
      ms = 0;

    // One decimal: 1234ms -> "1.2s"
    const long long tenth = (ms + 50) / 100; // rounded to 0.1s
    const long long sec = tenth / 10;
    const long long dec = tenth % 10;

    std::string out;
    out.reserve(32);
    out += std::to_string(sec);
    out.push_back('.');
    out += std::to_string(dec);
    out.push_back('s');
    return out;
  }

  // POSIX shell single-quote safe quoting (display only, not exec)
  inline std::string quote_for_display(std::string_view s)
  {
    if (s.empty())
      return "''";

    bool needs = false;
    for (char c : s)
    {
      if (c == ' ' || c == '\t' || c == '\n' || c == '"' || c == '\'' ||
          c == '\\' || c == '$' || c == '`')
      {
        needs = true;
        break;
      }
    }
    if (!needs)
      return std::string(s);

    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s)
    {
      if (c == '\'')
        out.append("'\\''");
      else
        out.push_back(c);
    }
    out.push_back('\'');
    return out;
  }

  inline std::string join_display_cmd(const std::vector<std::string> &argv)
  {
    std::string out;
    out.reserve(argv.size() * 8);

    for (std::size_t i = 0; i < argv.size(); ++i)
    {
      if (i)
        out.push_back(' ');

      out += quote_for_display(argv[i]);
    }

    return out;
  }

} // namespace vix::cli::util

#endif
