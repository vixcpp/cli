#ifndef VIX_CLI_STYLE_HPP
#define VIX_CLI_STYLE_HPP

#include <iostream>
#include <string>

namespace vix::cli::style
{
    // ANSI couleurs simples (Linux/macOS/WSL)
    inline constexpr const char *RESET = "\033[0m";
    inline constexpr const char *BOLD = "\033[1m";
    inline constexpr const char *RED = "\033[31m";
    inline constexpr const char *GREEN = "\033[32m";
    inline constexpr const char *YELLOW = "\033[33m";
    inline constexpr const char *GRAY = "\033[90m";

    // ---- Styled Output Helpers ---- //

    inline void error(const std::string &msg)
    {
        std::cerr << RED << "✖ " << msg << RESET << "\n";
    }

    inline void success(const std::string &msg)
    {
        std::cout << GREEN << "✔ " << msg << RESET << "\n";
    }

    inline void info(const std::string &msg)
    {
        std::cout << msg << "\n";
    }

    inline void hint(const std::string &msg)
    {
        std::cout << GRAY << "➜ " << msg << RESET << "\n";
    }

    inline void step(const std::string &msg)
    {
        std::cout << "  • " << msg << "\n";
    }
}

#endif // VIX_CLI_STYLE_HPP
