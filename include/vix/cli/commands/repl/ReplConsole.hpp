#ifndef VIX_RELP_CONSOLE_HPP
#define VIX_RELP_CONSOLE_HPP

#include <string_view>
#include <string>

namespace vix::cli::repl
{
  struct Console
  {
    static void set_editing(bool editing);
    static bool is_editing();
    static void set_redraw_callback(void (*fn)(void *ctx), void *ctx);
    static void print_out(std::string_view s);
    static void print_err(std::string_view s);
    static void println_out(std::string_view s = {});
    static void println_err(std::string_view s = {});
  };
}

#endif
