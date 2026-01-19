/**
 *
 *  @file Ui.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_UTIL_UI_HPP
#define VIX_CLI_UTIL_UI_HPP

#include <vix/cli/Style.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace vix::cli::util
{
  using namespace vix::cli::style;

  inline std::string quote(std::string_view s)
  {
    return std::string("\"") + std::string(s) + "\"";
  }

  inline void kv(std::ostream &os, std::string_view key, std::string_view value, int pad = 10)
  {
    std::ostringstream line;
    line << key;

    std::string k = line.str();
    if ((int)k.size() < pad)
      k.append((std::size_t)(pad - (int)k.size()), ' ');

    os << "    " << GRAY << "• " << RESET
       << GRAY << k << RESET
       << GRAY << ": " << RESET
       << YELLOW << BOLD << value << RESET << "\n";
  }

  inline void section(std::ostream &os, std::string_view title)
  {
    section_title(os, std::string(title));
  }

  inline void ok_line(std::ostream &os, std::string_view msg)
  {
    os << "  " << GREEN << "✔" << RESET << " " << msg << "\n";
  }

  inline void warn_line(std::ostream &os, std::string_view msg)
  {
    os << "  " << YELLOW << "!" << RESET << " " << msg << "\n";
  }

  inline void err_line(std::ostream &os, std::string_view msg)
  {
    os << "  " << RED << "✖" << RESET << " " << msg << "\n";
  }

  inline std::string dim(std::string_view s)
  {
    return std::string(GRAY) + std::string(s) + RESET;
  }

  inline std::string strong(std::string_view s)
  {
    return std::string(BOLD) + std::string(s) + RESET;
  }

  inline std::string faint_sep()
  {
    return std::string(GRAY) + "────────────────────────────────────────" + RESET;
  }

  inline void one_line_spacer(std::ostream &os)
  {
    os << "\n";
  }

  inline void pkg_line(
      std::ostream &os,
      std::string_view id,
      std::string_view latest,
      std::string_view desc,
      std::string_view repo)
  {
    os << "  " << CYAN << BOLD << id << RESET;

    if (!latest.empty())
    {
      os << "  "
         << GRAY << "(" << RESET
         << YELLOW << "latest" << RESET
         << GRAY << ": " << RESET
         << YELLOW << BOLD << latest << RESET
         << GRAY << ")" << RESET;
    }

    os << "\n";

    if (!desc.empty())
      os << "    " << GRAY << desc << RESET << "\n";

    if (!repo.empty())
    {
      os << "    "
         << GRAY << "repo: " << RESET
         << CYAN << UNDERLINE << repo << RESET
         << "\n";
    }
  }

  inline void dep_line(
      std::ostream &os,
      std::string_view id,
      std::string_view version,
      std::string_view commit,
      std::string_view repo)
  {
    os << "  " << CYAN << BOLD << id << RESET;

    // Secondary: version
    if (!version.empty())
    {
      os << "  "
         << GRAY << "(" << RESET
         << YELLOW << "version" << RESET
         << GRAY << ": " << RESET
         << YELLOW << BOLD << version << RESET
         << GRAY << ")" << RESET;
    }

    os << "\n";

    if (!commit.empty())
    {
      os << "    " << GRAY << "commit: " << RESET
         << YELLOW << commit << RESET << "\n";
    }

    if (!repo.empty())
    {
      os << "    " << GRAY << "repo: " << RESET
         << CYAN << UNDERLINE << repo << RESET << "\n";
    }
  }

} // namespace vix::cli::util

#endif
