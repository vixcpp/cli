#include <vix/cli/commands/repl/api/ReplApi.hpp>
#include <vix/cli/commands/repl/ReplUtils.hpp>
#include <vix/cli/commands/repl/ReplConsole.hpp>

#include <iostream>

namespace vix::cli::repl::api
{
    void print(std::string_view s)
    {
        vix::cli::repl::Console::print_out(s);
    }

    void println(std::string_view s)
    {
        vix::cli::repl::Console::println_out(s);
    }

    void eprint(std::string_view s)
    {
        vix::cli::repl::Console::print_err(s);
    }

    void eprintln(std::string_view s)
    {
        vix::cli::repl::Console::println_err(s);
    }

    void print_int(long long v)
    {
        vix::cli::repl::Console::print_out(std::to_string(v));
    }

    void println_int(long long v)
    {
        vix::cli::repl::Console::println_out(std::to_string(v));
    }

    std::optional<std::string> readln()
    {
        std::string s;
        if (!std::getline(std::cin, s))
            return std::nullopt;
        return vix::cli::repl::trim_copy(s);
    }

    void clear()
    {
        vix::cli::repl::clear_screen();
    }

    std::filesystem::path pwd()
    {
        return std::filesystem::current_path();
    }

    bool cd(const std::filesystem::path &p, std::string *err)
    {
        std::error_code ec;
        std::filesystem::current_path(p, ec);
        if (ec)
        {
            if (err)
                *err = ec.message();
            return false;
        }
        return true;
    }
}
