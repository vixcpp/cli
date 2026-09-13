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
    if (static_cast<int>(k.size()) < pad)
      k.append(static_cast<std::size_t>(pad - static_cast<int>(k.size())), ' ');

    os << "    " << ACCENT << "•" << RESET << " "
       << LABEL << k << RESET
       << ": "
       << value << "\n";
  }

  inline void section(std::ostream &os, std::string_view title)
  {
    section_title(os, std::string(title));
  }

  inline void ok_line(std::ostream &os, std::string_view msg)
  {
    os << "  " << SUCCESS << "✔" << RESET << " " << msg << "\n";
  }

  inline void warn_line(std::ostream &os, std::string_view msg)
  {
    os << "  " << WARNING << "!" << RESET << " " << msg << "\n";
  }

  inline void tip_line(std::ostream &os, std::string_view msg)
  {
    os << "\n"
       << ACCENT << "TIP:" << RESET << " " << msg << "\n\n";
  }

  inline void err_line(std::ostream &os, std::string_view msg)
  {
    os << "  " << ERROR_TEXT << "✖" << RESET << " " << msg << "\n";
  }

  inline std::string dim(std::string_view s)
  {
    return std::string(MUTED) + std::string(s) + RESET;
  }

  inline std::string strong(std::string_view s)
  {
    return std::string(BOLD) + std::string(s) + RESET;
  }

  inline void info_line(std::ostream &os, std::string_view msg)
  {
    os << "  " << ACCENT << "•" << RESET << " " << msg << "\n";
  }

  inline void info(std::ostream &os, std::string_view msg)
  {
    os << "  " << MUTED << "•" << RESET << " " << msg << "\n";
  }

  inline std::string faint_sep()
  {
    return std::string(MUTED) + "────────────────────────────────────────" + RESET;
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
    os << "  " << LABEL << id << RESET;

    if (!latest.empty())
    {
      os << "  ("
         << LABEL << "latest" << RESET
         << ": " << latest << ")";
    }

    os << "\n";

    if (!desc.empty())
      os << "    " << MUTED << desc << RESET << "\n";

    if (!repo.empty())
    {
      os << "    " << LABEL << "repo" << RESET << ": "
         << link(std::string(repo)) << "\n";
    }
  }

  inline void dep_line(
      std::ostream &os,
      std::string_view id,
      std::string_view version,
      std::string_view commit,
      std::string_view repo)
  {
    os << "  " << LABEL << id << RESET;

    // Secondary: version
    if (!version.empty())
    {
      os << "  ("
         << LABEL << "version" << RESET
         << ": " << version << ")";
    }

    os << "\n";

    if (!commit.empty())
    {
      os << "    " << LABEL << "commit" << RESET << ": "
         << commit << "\n";
    }

    if (!repo.empty())
    {
      os << "    " << LABEL << "repo" << RESET << ": "
         << link(std::string(repo)) << "\n";
    }
  }

} // namespace vix::cli::util

#endif
