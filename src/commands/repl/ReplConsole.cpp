#include <vix/cli/commands/repl/ReplConsole.hpp>
#include <iostream>
#include <atomic>

namespace vix::cli::repl
{
    namespace
    {
        std::atomic<bool> g_editing{false};
        void (*g_redraw_fn)(void *) = nullptr;
        void *g_redraw_ctx = nullptr;

        void before_print()
        {
            // If user is currently editing a line, move to a clean line first.
            if (g_editing.load())
            {
                std::cout << "\r\n";
            }
        }

        void after_print()
        {
            // Redraw prompt + current input line
            if (g_editing.load() && g_redraw_fn)
            {
                g_redraw_fn(g_redraw_ctx);
            }
        }
    }

    void Console::set_editing(bool editing) { g_editing.store(editing); }
    bool Console::is_editing() { return g_editing.load(); }

    void Console::set_redraw_callback(void (*fn)(void *ctx), void *ctx)
    {
        g_redraw_fn = fn;
        g_redraw_ctx = ctx;
    }

    void Console::print_out(std::string_view s)
    {
        before_print();
        std::cout << s << std::flush;
        after_print();
    }

    void Console::println_out(std::string_view s)
    {
        before_print();
        if (!s.empty())
            std::cout << s;
        std::cout << "\n"
                  << std::flush;
        after_print();
    }

    void Console::print_err(std::string_view s)
    {
        before_print();
        std::cerr << s << std::flush;
        after_print();
    }

    void Console::println_err(std::string_view s)
    {
        before_print();
        if (!s.empty())
            std::cerr << s;
        std::cerr << "\n"
                  << std::flush;
        after_print();
    }
}
