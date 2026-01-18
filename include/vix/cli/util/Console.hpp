/**
 *
 *  @file Console.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_CONSOLE_HPP
#define VIX_CLI_CONSOLE_HPP

#include <vix/cli/Style.hpp>
#include <string>

namespace vix::cli::util
{
  namespace style = vix::cli::style;

  static void log_header_if(bool quiet, const std::string &title)
  {
    if (quiet)
      return;
    style::info(title);
  }

  static void log_bullet_if(bool quiet, const std::string &line)
  {
    if (quiet)
      return;
    style::step(line);
  }

  static void log_hint_if(bool quiet, const std::string &msg)
  {
    if (quiet)
      return;
    style::hint(msg);
  }

  static void status_line(
      bool quiet, const std::string &tag,
      const std::string &msg)
  {
    if (quiet)
      return;
    std::cout << style::PAD << style::BOLD << style::CYAN << tag << style::RESET << " " << msg << "\n";
  }
} // namespace vix::cli::util

#endif
