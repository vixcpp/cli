#include <vix/cli/commands/repl/ReplDetail.hpp>
#include <vix/cli/commands/repl/ReplDispatcher.hpp>
#include <vix/cli/commands/repl/ReplHistory.hpp>
#include <vix/cli/commands/repl/ReplMath.hpp>
#include <vix/cli/commands/repl/ReplUtils.hpp>
#include <vix/cli/commands/repl/ReplFlow.hpp>
#include <vix/cli/commands/Dispatch.hpp>
#include <vix/utils/Logger.hpp>

#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

#ifndef VIX_CLI_VERSION
#define VIX_CLI_VERSION "dev"
#endif

namespace
{
    void print_banner()
    {
        std::cout
            << "Vix.cpp " << VIX_CLI_VERSION << " (CLI) — Modern C++ backend runtime\n"
#if defined(__clang__)
            << "[Clang " << __clang_major__ << "." << __clang_minor__ << "] "
#elif defined(__GNUC__)
            << "[GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << "] "
#else
            << "[C++ compiler] "
#endif
#if defined(_WIN32)
            << "on windows\n"
#elif defined(__APPLE__)
            << "on macos\n"
#else
            << "on linux\n"
#endif
            << "Exit: Ctrl+D or type .exit  |  Type .help for help\n";
    }

    static void print_commands_from_dispatcher()
    {
        auto &disp = vix::cli::dispatch::global();

        // group by category
        std::unordered_map<std::string, std::vector<const vix::cli::dispatch::Entry *>> groups;
        for (const auto &[name, e] : disp.entries())
            groups[e.category].push_back(&e);

        std::cout << "Commands:\n";
        for (auto &[cat, list] : groups)
        {
            std::cout << "  " << cat << ":\n";
            std::sort(list.begin(), list.end(),
                      [](auto a, auto b)
                      { return a->name < b->name; });

            for (auto *e : list)
                std::cout << "    " << e->name << "  - " << e->summary << "\n";
        }
    }

    static void print_help()
    {
        std::cout
            << "\n"
            << "REPL commands:\n"
            << "  .help                    Show this help\n"
            << "  .help <command>          Show help for a Vix command (ex: .help run)\n"
            << "  .exit                    Exit the REPL (or Ctrl+D)\n"
            << "  .version                 Print version\n"
            << "  .pwd                     Print current directory\n"
            << "  .cd <dir>                Change directory\n"
            << "  .clear                   Clear screen\n"
            << "  .history                 Show history\n"
            << "  .history clear           Clear history\n"
            << "\n"
            << "Calculator:\n"
            << "  = <expr>                 Evaluate expression (e.g. = 1+2*(3+4))\n"
            << "  .calc <expr>             Evaluate expression\n"
            << "\n";

        print_commands_from_dispatcher();

        std::cout << "\n";
    }

    bool is_calc_line(const std::string &line)
    {
        // "= ..." is calculator mode
        return vix::cli::repl::starts_with(line, "=");
    }

    std::string strip_calc_prefix(const std::string &line)
    {
        // remove leading '=' and trim
        std::string s = line.substr(1);
        return vix::cli::repl::trim_copy(s);
    }
}
namespace vix::commands::ReplCommand
{
    // Exposed symbol used by ReplCommand.cpp
    int repl_flow_run()
    {
        using vix::cli::repl::Dispatcher;
        using vix::cli::repl::History;
        using vix::cli::repl::ReplConfig;

        auto &logger = vix::utils::Logger::getInstance();
        (void)logger;

        ReplConfig cfg;
        cfg.historyFile = (vix::cli::repl::user_home_dir() / ".vix_history").string();

        Dispatcher dispatcher;
        History history(cfg.maxHistory);

        if (cfg.enableFileHistory)
        {
            std::string err;
            history.loadFromFile(cfg.historyFile, &err);
        }

        if (cfg.showBannerOnStart)
            print_banner();

        std::string line;

        while (true)
        {
            const fs::path cwd = fs::current_path();
            std::cout << vix::cli::repl::make_prompt(cwd) << std::flush;

            if (!std::getline(std::cin, line))
            {
                std::cout << "\n";
                break; // Ctrl+D
            }

            line = vix::cli::repl::trim_copy(line);
            if (line.empty())
                continue;

            // add to history
            history.add(line);

            // dot-commands
            if (line == ".exit")
                break;

            if (vix::cli::repl::starts_with(line, ".help"))
            {
                auto parts = vix::cli::repl::split_command_line(line);

                // ".help"
                if (parts.size() == 1)
                {
                    print_help();
                    continue;
                }

                // ".help <cmd>"
                const std::string cmd = parts[1];
                auto &disp = vix::cli::dispatch::global();

                if (!disp.has(cmd))
                {
                    std::cout << "Unknown command: " << cmd << "\n";
                    continue;
                }

                disp.help(cmd);
                std::cout << "\n";
                continue;
            }

            if (line == ".version")
            {
                std::cout << "Vix.cpp " << VIX_CLI_VERSION << "\n";
                continue;
            }

            if (line == ".pwd")
            {
                std::cout << fs::current_path().string() << "\n";
                continue;
            }

            if (line == ".commands")
            {
                auto &disp = vix::cli::dispatch::global();
                for (const auto &[name, e] : disp.entries())
                    std::cout << name << "  - " << e.summary << "\n";
                continue;
            }

            if (vix::cli::repl::starts_with(line, ".cd"))
            {
                auto parts = vix::cli::repl::split_command_line(line);
                if (parts.size() < 2)
                {
                    std::cout << "usage: .cd <dir>\n";
                    continue;
                }

                std::error_code ec;
                fs::current_path(parts[1], ec);
                if (ec)
                    std::cout << "cd: " << ec.message() << "\n";
                continue;
            }

            if (line == ".clear")
            {
                vix::cli::repl::clear_screen();
                if (cfg.showBannerOnClear)
                    print_banner();
                continue;
            }

            if (line == ".history")
            {
                const auto &items = history.items();
                for (std::size_t i = 0; i < items.size(); ++i)
                    std::cout << (i + 1) << "  " << items[i] << "\n";
                continue;
            }

            if (line == ".history clear")
            {
                history.clear();
                std::cout << "history cleared\n";
                continue;
            }

            // calculator mode
            if (cfg.enableCalculator && is_calc_line(line))
            {
                const std::string expr = strip_calc_prefix(line);
                if (expr.empty())
                {
                    std::cout << "usage: = <expr>\n";
                    continue;
                }

                std::string err;
                auto r = vix::cli::repl::eval_expression(expr, err);
                if (!r)
                {
                    std::cout << "calc error: " << err << "\n";
                    continue;
                }

                std::cout << r->formatted << "\n";
                continue;
            }

            if (cfg.enableCalculator && vix::cli::repl::starts_with(line, ".calc"))
            {
                auto parts = vix::cli::repl::split_command_line(line);
                if (parts.size() < 2)
                {
                    std::cout << "usage: .calc <expr>\n";
                    continue;
                }

                // reconstruct expr from raw line after ".calc"
                std::string expr = line.substr(5);
                expr = vix::cli::repl::trim_copy(expr);

                std::string err;
                auto r = vix::cli::repl::eval_expression(expr, err);
                if (!r)
                {
                    std::cout << "calc error: " << err << "\n";
                    continue;
                }

                std::cout << r->formatted << "\n";
                continue;
            }

            // normal dispatch (clean)
            auto parts = vix::cli::repl::split_command_line(line);
            if (parts.empty())
                continue;

            const std::string cmd = parts[0];
            std::vector<std::string> args(parts.begin() + 1, parts.end());

            auto &disp = vix::cli::dispatch::global();

            if (!disp.has(cmd))
            {
                std::cout << "Unknown command: " << cmd << " (type .help)\n";
                continue;
            }

            try
            {
                int code = disp.run(cmd, args);
                if (code != 0)
                    std::cout << "(exit code " << code << ")\n";
            }
            catch (const std::exception &ex)
            {
                std::cout << "error: " << ex.what() << "\n";
            }
        }

        // save history on exit
        if (cfg.enableFileHistory)
        {
            std::string err;
            if (!history.saveToFile(cfg.historyFile, &err))
                std::cout << "warning: " << err << "\n";
        }

        return 0;
    }
}