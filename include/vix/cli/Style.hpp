#ifndef VIX_CLI_STYLE_HPP
#define VIX_CLI_STYLE_HPP

#include <iostream>
#include <string>

namespace vix::cli::style
{
  // ANSI (Linux/macOS/WSL)
  inline constexpr const char *RESET = "\033[0m";
  inline constexpr const char *BOLD = "\033[1m";
  inline constexpr const char *RED = "\033[31m";
  inline constexpr const char *GREEN = "\033[32m";
  inline constexpr const char *YELLOW = "\033[33m";
  inline constexpr const char *CYAN = "\033[36m";
  inline constexpr const char *GRAY = "\033[90m";
  inline constexpr const char *PAD = "  ";

  inline void error(const std::string &msg)
  {
    std::cerr << PAD << RED << "✖ " << msg << RESET << "\n";
  }

  inline void success(const std::string &msg)
  {
    std::cout << PAD << GREEN << "✔ " << msg << RESET << "\n";
  }

  inline void info(const std::string &msg)
  {
    std::cout << PAD << msg << "\n";
  }

  inline void hint(const std::string &msg)
  {
    std::cout << PAD << GRAY << "➜ " << msg << RESET << "\n";
  }

  inline void step(const std::string &msg)
  {
    std::cout << PAD << "  • " << msg << "\n";
  }

  inline void section_title(std::ostream &out, const std::string &label)
  {
    out << "\n"
        << PAD << BOLD << CYAN << label << RESET << "\n";
  }

  inline void dim_note(std::ostream &out, const std::string &label)
  {
    out << PAD << GRAY << label << RESET << "\n";
  }

  inline std::string link(const std::string &url)
  {
    return std::string(GREEN) + url + RESET;
  }

} // namespace vix::cli::style

#endif // CLI_STYLE_HPP
