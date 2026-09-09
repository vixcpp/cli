/**
 *
 *  @file Style.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_STYLE_HPP
#define VIX_CLI_STYLE_HPP

#include <iostream>
#include <string>

namespace vix::cli::style
{
  // ANSI (Linux/macOS/WSL).  Keep semantic text readable on both dark and
  // light themes: required information uses the terminal default foreground
  // unless its meaning needs a strong accent.  In particular, do not use
  // bright-black (90) for paths, code, labels, or actionable guidance.
  inline constexpr const char *RESET = "\033[0m";
  inline constexpr const char *BOLD = "\033[1m";
  inline constexpr const char *UNDERLINE = "\033[4m";
  inline constexpr const char *ERROR = "\033[1;91m";
  inline constexpr const char *WARNING = "\033[1;93m";
  inline constexpr const char *SUCCESS = "\033[1;92m";
  inline constexpr const char *ACCENT = "\033[1;96m";
  inline constexpr const char *MUTED = RESET;
  inline constexpr const char *PATH = ACCENT;
  inline constexpr const char *CODE = RESET;
  inline constexpr const char *LINE_NUMBER = RESET;
  inline constexpr const char *LABEL = BOLD;

  // Compatibility aliases for existing callers.  New diagnostic code should
  // prefer the semantic names above.
  inline constexpr const char *RED = ERROR;
  inline constexpr const char *GREEN = SUCCESS;
  inline constexpr const char *YELLOW = WARNING;
  inline constexpr const char *CYAN = ACCENT;
  inline constexpr const char *GRAY = MUTED;
  inline constexpr const char *MAGENTA = "\033[1;95m";
  inline constexpr const char *PAD = "  ";

  inline void error(const std::string &msg)
  {
    std::cerr << PAD << ERROR << "✖ " << msg << RESET << "\n";
  }

  inline void success(const std::string &msg)
  {
    std::cout << PAD << SUCCESS << "✔" << RESET << " " << msg << "\n";
  }

  inline void info(const std::string &msg)
  {
    std::cout << PAD << msg << "\n";
  }

  inline void hint(const std::string &msg)
  {
    std::cout << PAD << WARNING << "➜" << RESET << " " << msg << "\n";
  }

  inline void step(const std::string &msg)
  {
    std::cout << PAD << "  • " << msg << "\n";
  }

  inline void section_title(std::ostream &out, const std::string &label)
  {
    out << PAD << ACCENT << label << RESET << "\n";
  }

  inline void blank(std::ostream &out = std::cout)
  {
    out << "\n";
  }

  inline void dim_note(std::ostream &out, const std::string &label)
  {
    out << PAD << MUTED << label << RESET << "\n";
  }

  inline std::string link(const std::string &url)
  {
    return std::string(ACCENT) + url + RESET;
  }

} // namespace vix::cli::style

#endif // VIX_CLI_STYLE_HPP
