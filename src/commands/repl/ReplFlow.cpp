#include <vix/cli/commands/repl/ReplDetail.hpp>
#include <vix/cli/commands/repl/ReplDispatcher.hpp>
#include <vix/cli/commands/repl/ReplHistory.hpp>
#include <vix/cli/commands/repl/ReplMath.hpp>
#include <vix/cli/commands/repl/ReplUtils.hpp>
#include <vix/cli/commands/repl/ReplFlow.hpp>
#include <vix/cli/commands/Dispatch.hpp>
#include <vix/cli/commands/repl/ReplLineEditor.hpp>
#include <vix/cli/commands/repl/api/Vix.hpp>
#include <vix/cli/commands/repl/api/ReplCallParser.hpp>
#include <vix/cli/commands/repl/api/ReplApi.hpp>
#include <vix/utils/Logger.hpp>

#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <atomic>
#include <string>
#include <cstring>
#include <chrono>

namespace fs = std::filesystem;

#ifndef VIX_CLI_VERSION
#define VIX_CLI_VERSION "dev"
#endif

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#endif

namespace
{
    // Ctrl keys (lowercase / not "majuscule")
    // In terminals:
    //   Ctrl+C -> 0x03
    //   Ctrl+D -> 0x04
    //   Ctrl+L -> 0x0C
    constexpr unsigned char KEY_CTRL_C = 0x03;
    constexpr unsigned char KEY_CTRL_D = 0x04;
    constexpr unsigned char KEY_CTRL_L = 0x0C;
    constexpr unsigned char KEY_BACKSPACE_1 = 0x08;
    constexpr unsigned char KEY_BACKSPACE_2 = 0x7F;
    constexpr unsigned char KEY_ENTER_1 = '\n';
    constexpr unsigned char KEY_ENTER_2 = '\r';

    enum class ReadStatus
    {
        Ok,
        Eof,         // Ctrl+D on empty line
        Interrupted, // Ctrl+C
        Clear        // Ctrl+L
    };

#ifndef _WIN32
    struct TerminalRawMode
    {
        termios old{};
        bool enabled = false;

        TerminalRawMode()
        {
            if (!isatty(STDIN_FILENO))
                return;

            if (tcgetattr(STDIN_FILENO, &old) != 0)
                return;

            termios raw = old;

            // raw-ish: disable canonical & echo, keep signals disabled (we handle Ctrl+C ourselves)
            raw.c_lflag &= static_cast<unsigned>(~(ICANON | ECHO));
            raw.c_lflag &= static_cast<unsigned>(~(IEXTEN));
            raw.c_lflag &= static_cast<unsigned>(~(ISIG));

            // input tweaks
            raw.c_iflag &= static_cast<unsigned>(~(IXON | ICRNL));

            // one byte at a time
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;

            if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0)
                enabled = true;
        }

        ~TerminalRawMode()
        {
            if (enabled)
                tcsetattr(STDIN_FILENO, TCSANOW, &old);
        }
    };

#endif

    static bool looks_like_math_expr(const std::string &s)
    {
        auto t = vix::cli::repl::trim_copy(s);
        if (t.empty())
            return false;

        // If first char is alpha or '_' => command-like
        const unsigned char first = static_cast<unsigned char>(t.front());
        if (std::isalpha(first) || t.front() == '_')
            return false;

        // Avoid obvious code-ish patterns
        for (char c : t)
        {
            if (c == ';' || c == '{' || c == '}' || c == '#' || c == '=')
                return false;
        }

        // Accept math only if starts with a math char
        const char c0 = t.front();
        if (std::isdigit(static_cast<unsigned char>(c0)) || c0 == '.' || c0 == '(' || c0 == '+' || c0 == '-')
            return true;

        return false;
    }

    static void print_banner()
    {
        std::cout
            << "Vix.cpp " << VIX_CLI_VERSION << " (CLI) — Modern C++ backend runtime ";
#if defined(__clang__)
        std::cout << "[Clang " << __clang_major__ << "." << __clang_minor__ << "] ";
#elif defined(__GNUC__)
        std::cout << "[GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << "] ";
#else
        std::cout << "[C++ compiler] ";
#endif

#if defined(_WIN32)
        std::cout << "on windows\n";
#elif defined(__APPLE__)
        std::cout << "on macos\n";
#else
        std::cout << "on linux\n";
#endif

        std::cout
            << "Exit: Ctrl+C / Ctrl+D or type exit | Clear: Ctrl+L or clear | Type help for help\n";
    }

    static void print_commands_from_dispatcher()
    {
        auto &disp = vix::cli::dispatch::global();

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
            << "  help                    Show this help\n"
            << "  help <command>          Show help for a Vix command (ex: help run)\n"
            << "  exit                    Exit the REPL (or Ctrl+C / Ctrl+D)\n"
            << "  version                 Print version\n"
            << "  pwd                     Print current directory\n"
            << "  cd <dir>                Change directory\n"
            << "  clear                   Clear screen (or Ctrl+L)\n"
            << "  history                 Show history\n"
            << "  history clear           Clear history\n"
            << "\n"
            << "Math:\n"
            << "  <expr>                  Evaluate expression (e.g. 1+2*(3+4))\n"
            << "  calc <expr>             Evaluate expression (explicit)\n"
            << "\n";

        print_commands_from_dispatcher();
        std::cout << "\n";
    }

    static std::string strip_prefix(std::string s, const std::string &prefix)
    {
        if (s.rfind(prefix, 0) == 0)
            s = s.substr(prefix.size());
        return vix::cli::repl::trim_copy(s);
    }

    static std::string to_string(const vix::cli::repl::api::CallValue &v)
    {
        if (v.is_string())
            return v.as_string();
        if (v.is_bool())
            return v.as_bool() ? "true" : "false";
        if (v.is_int())
            return std::to_string(v.as_int());
        if (v.is_double())
            return std::to_string(v.as_double());
        return "null";
    }

    static bool contains_math_ops(const std::string &s)
    {
        for (char c : s)
        {
            if (c == '+' || c == '-' || c == '*' || c == '/' || c == '(' || c == ')')
                return true;
        }
        return false;
    }

    static std::string arg_to_text(const vix::cli::repl::api::CallExpr &call, size_t i, std::string &err)
    {
        using namespace vix::cli::repl;

        if (i >= call.args.size())
            return {};

        // If raw exists and looks like an expression, evaluate it
        if (i < call.args_raw.size())
        {
            const std::string &raw = call.args_raw[i];
            if (contains_math_ops(raw))
            {
                auto r = eval_expression(raw, err);
                if (!r)
                    return {};
                return r->formatted;
            }
        }

        // fallback: literal stringify
        return to_string(call.args[i]);
    }

    static bool invoke_call(const vix::cli::repl::api::CallExpr &call,
                            vix::cli::repl::api::Vix &VixObj)
    {
        auto join_all = [&](std::string_view sep, std::string &err) -> std::optional<std::string>
        {
            std::string out;
            for (size_t i = 0; i < call.args.size(); ++i)
            {
                if (i)
                    out += sep;

                auto t = arg_to_text(call, i, err);
                if (!err.empty())
                    return std::nullopt;

                out += t;
            }
            return out;
        };

        auto emit_joined = [&](auto emit, std::string_view sep = " ") -> bool
        {
            std::string err;
            auto txt = join_all(sep, err);
            if (!txt)
            {
                vix::cli::repl::api::println(std::string("error: ") + err);
                return false;
            }
            emit(*txt);
            return true;
        };

        // GLOBAL: print / println
        if (call.object.empty())
        {
            const std::string &fn = call.callee;

            if (fn == "print")
            {
                emit_joined([](const std::string &s)
                            { vix::cli::repl::api::print(s); });
                return true;
            }
            if (fn == "println")
            {
                emit_joined([](const std::string &s)
                            { vix::cli::repl::api::println(s); });
                return true;
            }
            if (fn == "eprint")
            {
                emit_joined([](const std::string &s)
                            { vix::cli::repl::api::eprint(s); });
                return true;
            }
            if (fn == "eprintln")
            {
                emit_joined([](const std::string &s)
                            { vix::cli::repl::api::eprintln(s); });
                return true;
            }

            if (fn == "cwd")
            {
                vix::cli::repl::api::println(VixObj.cwd().string());
                return true;
            }

            if (fn == "pid")
            {
                vix::cli::repl::api::println_int((long long)VixObj.pid());
                return true;
            }

            return false; // not a known global call
        }

        const std::string obj = call.object;
        if (obj != "Vix" && obj != "vix")
            return false;

        const std::string &m = call.member;

        if (m == "cd")
        {
            if (call.args.size() != 1 || !call.args[0].is_string())
            {
                vix::cli::repl::api::println("error: Vix.cd(path:string)");
                return true;
            }
            auto r = VixObj.cd(call.args[0].as_string());
            if (r.code != 0 && !r.message.empty())
                vix::cli::repl::api::println("error: " + r.message);
            return true;
        }

        if (m == "cwd")
        {
            vix::cli::repl::api::println(VixObj.cwd().string());
            return true;
        }

        if (m == "mkdir")
        {
            if (call.args.empty() || !call.args[0].is_string())
            {
                vix::cli::repl::api::println("error: Vix.mkdir(path:string, recursive?:bool)");
                return true;
            }
            bool recursive = true;
            if (call.args.size() >= 2)
            {
                if (!call.args[1].is_bool())
                {
                    vix::cli::repl::api::println("error: recursive must be bool");
                    return true;
                }
                recursive = call.args[1].as_bool();
            }
            auto r = VixObj.mkdir(call.args[0].as_string(), recursive);
            if (r.code != 0 && !r.message.empty())
                vix::cli::repl::api::println("error: " + r.message);
            return true;
        }

        if (m == "env")
        {
            if (call.args.size() != 1 || !call.args[0].is_string())
            {
                vix::cli::repl::api::println("error: Vix.env(key:string)");
                return true;
            }
            auto v = VixObj.env(call.args[0].as_string());
            if (!v)
                vix::cli::repl::api::println("null");
            else
                vix::cli::repl::api::println(*v);
            return true;
        }

        if (m == "pid")
        {
            vix::cli::repl::api::println_int((long long)VixObj.pid());
            return true;
        }

        if (m == "exit")
        {
            int code = 0;
            if (!call.args.empty())
            {
                if (!call.args[0].is_int())
                {
                    vix::cli::repl::api::println("error: Vix.exit(code:int)");
                    return true;
                }
                code = (int)call.args[0].as_int();
            }
            VixObj.exit(code);
            return true;
        }

        return false;
    }

} // namespace

namespace vix::commands::ReplCommand
{
    int repl_flow_run()
    {
        using vix::cli::repl::Dispatcher;
        using vix::cli::repl::History;
        using vix::cli::repl::ReplConfig;

        auto &logger = vix::utils::Logger::getInstance();
        (void)logger;

        ReplConfig cfg;
        cfg.historyFile = (vix::cli::repl::user_home_dir() / ".vix_history").string();

        History history(cfg.maxHistory);
        vix::cli::repl::api::Vix VixObj(&history);

        if (cfg.enableFileHistory)
        {
            std::string err;
            history.loadFromFile(cfg.historyFile, &err);
        }

        if (cfg.showBannerOnStart)
            print_banner();

#ifndef _WIN32
        TerminalRawMode rawMode; // enables Ctrl+C/Ctrl+D/Ctrl+L reliably without Enter
        // History navigation state
        int histIndex = -1;    // -1 means "not browsing"
        std::string histDraft; // keep current typed line before browsing
#endif

        while (true)
        {
            const fs::path cwd = fs::current_path();
            const std::string prompt = vix::cli::repl::make_prompt(cwd);

            std::string line;

#ifndef _WIN32
            // HISTORY (↑ / ↓)
            auto onHistoryUp = [&](std::string &ioLine) -> bool
            {
                const auto &items = history.items();
                if (items.empty())
                    return false;

                if (histIndex == -1)
                {
                    histDraft = ioLine;
                    histIndex = (int)items.size() - 1;
                    ioLine = items[(size_t)histIndex];
                    return true;
                }

                if (histIndex > 0)
                {
                    --histIndex;
                    ioLine = items[(size_t)histIndex];
                    return true;
                }

                return false;
            };

            auto onHistoryDown = [&](std::string &ioLine) -> bool
            {
                const auto &items = history.items();
                if (items.empty())
                    return false;

                if (histIndex == -1)
                    return false;

                if (histIndex < (int)items.size() - 1)
                {
                    ++histIndex;
                    ioLine = items[(size_t)histIndex];
                    return true;
                }

                histIndex = -1;
                ioLine = histDraft;
                return true;
            };

            // Reset history browsing as soon as user starts a new fresh command after execution
            // (we also reset it when user presses Enter below)
            auto reset_history_browse = [&]()
            {
                histIndex = -1;
                histDraft.clear();
            };

            // COMPLETION (TAB)
            // - commands
            // - options per command
            // - cd path completion
            auto completer = [&](const std::string &current) -> vix::cli::repl::CompletionResult
            {
                using vix::cli::repl::CompletionResult;

                CompletionResult r;

                std::string trimmed = vix::cli::repl::trim_copy(current);
                auto parts = vix::cli::repl::split_command_line(trimmed);

                const auto &disp = vix::cli::dispatch::global();

                auto starts_with = [](const std::string &s, const std::string &p) -> bool
                {
                    return s.rfind(p, 0) == 0;
                };

                // Builtins and global options (REPL commands)
                const std::vector<std::string> builtins = {
                    "help", "exit", "clear", "pwd", "cd",
                    "history", "version", "commands", "calc"};

                // Options by command (extend as you want)
                auto options_for = [&](const std::string &cmd) -> std::vector<std::string>
                {
                    // global common flags
                    std::vector<std::string> base = {"--help"};

                    // examples based on your Vix CLI
                    if (cmd == "check" || cmd == "run" || cmd == "tests" || cmd == "verify")
                    {
                        base.push_back("--san");
                        base.push_back("--asan");
                        base.push_back("--ubsan");
                        base.push_back("--tsan");
                        base.push_back("--lsan");
                    }

                    if (cmd == "check")
                    {
                        base.push_back("--release");
                        base.push_back("--debug");
                    }

                    return base;
                };

                // Utility: unique/sort
                auto normalize_list = [](std::vector<std::string> &v)
                {
                    std::sort(v.begin(), v.end());
                    v.erase(std::unique(v.begin(), v.end()), v.end());
                };

                // 1) COMMAND completion (first token)
                if (parts.size() <= 1 && trimmed.find(' ') == std::string::npos)
                {
                    const std::string prefix = trimmed;
                    std::vector<std::string> matches;

                    // dispatcher commands
                    for (const auto &[name, e] : disp.entries())
                    {
                        (void)e;
                        if (starts_with(name, prefix))
                            matches.push_back(name);
                    }

                    // builtins
                    for (const auto &b : builtins)
                    {
                        if (starts_with(b, prefix))
                            matches.push_back(b);
                    }

                    normalize_list(matches);

                    if (matches.size() == 1)
                    {
                        r.changed = true;
                        r.newLine = matches[0] + " ";
                        return r;
                    }
                    if (matches.size() > 1)
                    {
                        r.suggestions = matches;
                        return r;
                    }
                    return r;
                }

                if (parts.empty())
                    return r;

                const std::string cmd = parts[0];

                // 2) CONTEXT: cd <path> completion
                if (cmd == "cd" && trimmed.size() >= 2)
                {
                    // Determine what user is completing: the last token (path)
                    std::string pathPrefix;
                    if (parts.size() >= 2)
                        pathPrefix = parts.back();
                    else
                        pathPrefix.clear();

                    namespace fs2 = std::filesystem;

                    fs2::path baseDir = ".";
                    std::string filePrefix = pathPrefix;

                    // If prefix contains '/', split base dir / prefix
                    auto pos = pathPrefix.find_last_of("/\\");
                    if (pos != std::string::npos)
                    {
                        baseDir = fs2::path(pathPrefix.substr(0, pos + 1));
                        filePrefix = pathPrefix.substr(pos + 1);
                    }

                    std::vector<std::string> dirs;

                    std::error_code ec;
                    if (fs2::exists(baseDir, ec) && fs2::is_directory(baseDir, ec))
                    {
                        for (auto &it : fs2::directory_iterator(baseDir, ec))
                        {
                            if (ec)
                                break;

                            if (!it.is_directory(ec))
                                continue;

                            std::string name = it.path().filename().string();
                            if (starts_with(name, filePrefix))
                            {
                                std::string full = (baseDir / name).string();
                                // Normalize to trailing slash (nice UX)
                                if (!full.empty() && full.back() != '/')
                                    full.push_back('/');
                                dirs.push_back(full);
                            }
                        }
                    }

                    normalize_list(dirs);

                    if (dirs.size() == 1)
                    {
                        // Replace last token with the completed dir
                        // rebuild line: "cd " + dirs[0]
                        r.changed = true;
                        r.newLine = "cd " + dirs[0];
                        return r;
                    }

                    if (dirs.size() > 1)
                    {
                        r.suggestions = dirs;
                        return r;
                    }

                    return r;
                }

                // 3) OPTIONS completion (TAB after cmd or when typing -)
                const std::vector<std::string> opts = options_for(cmd);

                // detect if we are completing an option (last token starts with '-')
                std::string lastTok = parts.back();
                if (!lastTok.empty() && lastTok[0] == '-')
                {
                    std::vector<std::string> matches;
                    for (const auto &o : opts)
                    {
                        if (starts_with(o, lastTok))
                            matches.push_back(o);
                    }
                    normalize_list(matches);

                    if (matches.size() == 1)
                    {
                        // Replace last token
                        // naive rebuild:
                        std::string rebuilt;
                        for (size_t i = 0; i + 1 < parts.size(); ++i)
                        {
                            rebuilt += parts[i];
                            rebuilt += " ";
                        }
                        rebuilt += matches[0];
                        rebuilt += " ";
                        r.changed = true;
                        r.newLine = rebuilt;
                        return r;
                    }

                    if (matches.size() > 1)
                    {
                        r.suggestions = matches;
                        return r;
                    }

                    return r;
                }

                // If user pressed TAB right after "cmd " show options list
                if (trimmed.size() >= cmd.size() + 1 && trimmed == (cmd + " "))
                {
                    if (!opts.empty())
                    {
                        r.suggestions = opts;
                        normalize_list(r.suggestions);
                        return r;
                    }
                }

                return r;
            };

            vix::cli::repl::ReadStatus st =
                vix::cli::repl::read_line_edit(prompt, line, completer, onHistoryUp, onHistoryDown);

            if (st == vix::cli::repl::ReadStatus::Interrupted)
                break;
            if (st == vix::cli::repl::ReadStatus::Eof)
                break;
            if (st == vix::cli::repl::ReadStatus::Clear)
            {
                vix::cli::repl::clear_screen();
                if (cfg.showBannerOnClear)
                    print_banner();
                continue;
            }

            // Enter pressed -> reset browse state
            reset_history_browse();

            line = vix::cli::repl::trim_copy(line);
            // 1) API CALL PARSER: println("hi"), Vix.cd(".."), etc.
            if (vix::cli::repl::api::looks_like_call(line))
            {
                auto pr = vix::cli::repl::api::parse_call(line);
                if (pr.ok)
                {
                    if (invoke_call(pr.expr, VixObj))
                    {
                        // if user requested exit via Vix.exit(...)
                        if (VixObj.exit_requested())
                            break;
                        continue;
                    }
                    // parsed call but unknown function/member
                    if (!pr.expr.object.empty())
                        std::cout << "Unknown call: " << pr.expr.object << "." << pr.expr.member << "(...)\n";
                    else
                        std::cout << "Unknown call: " << pr.expr.callee << "(...)\n";
                    continue;
                }
                else
                {
                    std::cout << "error: " << pr.error << "\n";
                    continue;
                }
            }

#else
            std::cout << prompt << std::flush;
            if (!std::getline(std::cin, line))
            {
                std::cout << "\n";
                break;
            }
            line = vix::cli::repl::trim_copy(line);
#endif

            if (line.empty())
                continue;

            // History
            history.add(line);

            if (line == "exit" || line == ".exit") // keep legacy alias
                break;

            if (line == "help" || vix::cli::repl::starts_with(line, "help ") ||
                line == ".help" || vix::cli::repl::starts_with(line, ".help "))
            {
                auto normalized = line;
                if (vix::cli::repl::starts_with(normalized, ".help"))
                    normalized.erase(0, 1); // remove leading '.'

                auto parts = vix::cli::repl::split_command_line(normalized);

                if (parts.size() == 1)
                {
                    print_help();
                    continue;
                }

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

            if (line == "version" || line == ".version")
            {
                std::cout << "Vix.cpp " << VIX_CLI_VERSION << "\n";
                continue;
            }

            if (line == "pwd" || line == ".pwd")
            {
                std::cout << fs::current_path().string() << "\n";
                continue;
            }

            if (line == "clear" || line == ".clear")
            {
                vix::cli::repl::clear_screen();
                if (cfg.showBannerOnClear)
                    print_banner();
                continue;
            }

            if (line == "history" || line == ".history")
            {
                const auto &items = history.items();
                for (std::size_t i = 0; i < items.size(); ++i)
                    std::cout << (i + 1) << "  " << items[i] << "\n";
                continue;
            }

            if (line == "history clear" || line == ".history clear")
            {
                history.clear();
                std::cout << "history cleared\n";
                continue;
            }

            if (line == "commands" || line == ".commands")
            {
                auto &disp = vix::cli::dispatch::global();
                for (const auto &[name, e] : disp.entries())
                    std::cout << name << "  - " << e.summary << "\n";
                continue;
            }

            if (vix::cli::repl::starts_with(line, "cd ") || vix::cli::repl::starts_with(line, ".cd "))
            {
                auto normalized = line;
                if (vix::cli::repl::starts_with(normalized, ".cd"))
                    normalized.erase(0, 1);

                auto parts = vix::cli::repl::split_command_line(normalized);
                if (parts.size() < 2)
                {
                    std::cout << "usage: cd <dir>\n";
                    continue;
                }

                std::error_code ec;
                fs::current_path(parts[1], ec);
                if (ec)
                    std::cout << "cd: " << ec.message() << "\n";
                continue;
            }

            if (cfg.enableCalculator && (vix::cli::repl::starts_with(line, "calc ") || vix::cli::repl::starts_with(line, ".calc ")))
            {
                auto expr = line;
                if (vix::cli::repl::starts_with(expr, ".calc"))
                    expr.erase(0, 1); // remove dot
                expr = strip_prefix(expr, "calc");

                if (expr.empty())
                {
                    std::cout << "usage: calc <expr>\n";
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

            auto parts = vix::cli::repl::split_command_line(line);
            if (parts.empty())
                continue;

            const std::string cmd = parts[0];
            std::vector<std::string> args(parts.begin() + 1, parts.end());

            auto &disp = vix::cli::dispatch::global();

            // 1) Known Vix command -> run
            if (disp.has(cmd))
            {
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
                continue;
            }

            // 2) Not a known command -> try math directly (no '=' needed)
            if (cfg.enableCalculator && looks_like_math_expr(line))
            {
                std::string err;
                auto r = vix::cli::repl::eval_expression(line, err);
                if (r)
                {
                    std::cout << r->formatted << "\n";
                    continue;
                }

                if (!err.empty())
                {
                    std::cout << "error: " << err << "\n";
                    continue;
                }
            }

            std::cout << "Unknown command: " << cmd << " (type help)\n";
        }

        // save history on exit
        if (cfg.enableFileHistory)
        {
            std::string err;
            if (!history.saveToFile(cfg.historyFile, &err))
                std::cout << "warning: " << err << "\n";
        }

        return VixObj.exit_requested() ? VixObj.exit_code() : 0;
    }
} // namespace vix::commands::ReplCommand
