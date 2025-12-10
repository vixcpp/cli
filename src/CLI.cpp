/**
 * @file CLI.cpp
 * @brief Core implementation of the Vix.cpp Command Line Interface (CLI).
 *
 * This module provides the main entry point for the Vix.cpp CLI tool,
 * allowing developers to create, build, and run C++ projects powered by Vix.
 *
 * ## Available Commands
 * - `vix new <name>` — Create a new Vix project template.
 * - `vix build [name]` — Build an existing project or application.
 * - `vix run [name] [--args]` — Run a project or service.
 * - `vix version` — Display the current CLI version.
 * - `vix help` — Show this help message.
 *
 * ## Architecture
 * The CLI uses an internal hash map (`commands_`) that associates each
 * command string with a callable function object:
 *
 * ```cpp
 * std::unordered_map<std::string, std::function<int(std::vector<std::string>)>>;
 * ```
 *
 * This design makes it easy to extend the CLI — simply register a new
 * command and its associated function during initialization.
 *
 * ## Error Handling
 * All command executions are wrapped in try/catch blocks.
 * If an exception occurs, it is logged using the global `Vix::Logger`
 * with clear contextual information (module name, severity level, and message).
 *
 * ## Example usage
 * ```bash
 * vix new blog
 * vix build blog --config Release
 * vix run blog -- --port 8080
 * ```
 *
 * @namespace Vix
 * @class CLI
 * @details
 * The `Vix::CLI` class encapsulates the logic for command registration,
 * argument parsing, and runtime execution of subcommands.
 *
 * It also provides convenient aliases such as:
 * - `-h` or `--help` → same as `help`
 * - `-v` or `--version` → same as `version`
 *
 * @note
 * All user-facing messages are displayed through the central
 * `Vix::Logger` instance, which ensures consistent formatting
 * and module-based colorized output.
 *
 * @version 1.3.0
 * @date 2025
 * @authors
 * SoftAdAstra
 */
#include <vix/cli/CLI.hpp>
#include <vix/cli/commands/NewCommand.hpp>
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/cli/commands/DevCommand.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Logger.hpp>

#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <optional>
#include <cstdlib>   // std::getenv
#include <algorithm> // std::transform
#include <cctype>    // std::tolower

namespace vix
{
    using Logger = vix::utils::Logger;
    using namespace vix::cli::style;

    namespace
    {
        // -------------------------------------------------------------
        // Helpers for log-level parsing
        // -------------------------------------------------------------

        std::string to_lower_copy(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        std::optional<Logger::Level> parse_log_level(const std::string &raw)
        {
            const std::string s = to_lower_copy(raw);

            if (s == "trace")
                return Logger::Level::TRACE;
            if (s == "debug")
                return Logger::Level::DEBUG;
            if (s == "info")
                return Logger::Level::INFO;
            if (s == "warn" || s == "warning")
                return Logger::Level::WARN;
            if (s == "error" || s == "err")
                return Logger::Level::ERROR;
            if (s == "critical" || s == "fatal")
                return Logger::Level::CRITICAL;

            return std::nullopt;
        }

        void apply_log_level_from_env(Logger &logger)
        {
            if (const char *env = std::getenv("VIX_LOG_LEVEL"))
            {
                std::string value(env);
                if (auto lvl = parse_log_level(value))
                {
                    logger.setLevel(*lvl);
                }
                else
                {
                    std::cerr << "vix: invalid VIX_LOG_LEVEL value '" << value
                              << "'. Expected one of: trace, debug, info, warn, error, critical.\n";
                }
            }
        }

        void apply_log_level_from_flag(Logger &logger, const std::string &value)
        {
            if (auto lvl = parse_log_level(value))
            {
                logger.setLevel(*lvl);
            }
            else
            {
                std::cerr << "vix: invalid --log-level value '" << value
                          << "'. Expected one of: trace, debug, info, warn, error, critical.\n";
            }
        }

    } // namespace

    // -----------------------------------------------------------------
    // CLI constructor — register commands
    // -----------------------------------------------------------------
    CLI::CLI()
    {
        // Base commands
        commands_["help"] = [this](auto args)
        { return help(args); };
        commands_["version"] = [this](auto args)
        { return version(args); };

        // Main CLI commands
        commands_["new"] = [](auto args)
        { return commands::NewCommand::run(args); };
        commands_["run"] = [](auto args)
        { return commands::RunCommand::run(args); };
        commands_["build"] = [](auto args)
        { return commands::BuildCommand::run(args); };
        commands_["dev"] = [](auto args)
        {
            return commands::DevCommand::run(args);
        };

        // Useful aliases (treated as commands)
        commands_["-h"] = [this](auto args)
        { return help(args); };
        commands_["--help"] = [this](auto args)
        { return help(args); };
        commands_["-v"] = [this](auto args)
        { return version(args); };
        commands_["--version"] = [this](auto args)
        { return version(args); };

        // Internal demo command (optional)
        commands_["hello"] = [](auto)
        {
            auto &logger = Logger::getInstance();
            logger.logModule("CLI", Logger::Level::INFO, "Hello from Vix.cpp 👋");
            return 0;
        };
    }

    // -----------------------------------------------------------------
    // CLI::run — entry point for the vix binary
    // -----------------------------------------------------------------
    int CLI::run(int argc, char **argv)
    {
        auto &logger = Logger::getInstance();

        if (argc < 2)
        {
            // No command → global help
            return help({});
        }

        // ----------------------------------------------------------
        // 1) Global options: --verbose / --quiet / -q / --log-level
        //    Syntax:
        //      vix [GLOBAL OPTIONS] <COMMAND> [ARGS...]
        //    Examples:
        //      vix --verbose run myapp
        //      vix --quiet build
        //      VIX_LOG_LEVEL=debug vix run myapp
        //      vix --log-level warn run myapp
        // ----------------------------------------------------------
        enum class VerbosityMode
        {
            Default,
            Verbose,
            Quiet
        };

        VerbosityMode verbosity = VerbosityMode::Default;
        std::optional<std::string> logLevelFlag;
        int index = 1;

        // 1.a Apply environment variable first (base level)
        //     CLI flags may override this later.
        apply_log_level_from_env(logger);

        // 1.b Parse leading global options
        while (index < argc)
        {
            std::string arg = argv[index];

            if (arg == "--verbose")
            {
                verbosity = VerbosityMode::Verbose;
                ++index;
                continue;
            }

            if (arg == "--quiet" || arg == "-q")
            {
                verbosity = VerbosityMode::Quiet;
                ++index;
                continue;
            }

            if (arg == "--log-level")
            {
                if (index + 1 >= argc)
                {
                    std::cerr << "vix: --log-level requires a value (trace|debug|info|warn|error|critical).\n";
                    return 1;
                }
                logLevelFlag = argv[index + 1];
                index += 2;
                continue;
            }

            // Support --log-level=info style
            constexpr const char prefix[] = "--log-level=";
            if (arg.rfind(prefix, 0) == 0)
            {
                std::string value = arg.substr(sizeof(prefix) - 1);
                if (value.empty())
                {
                    std::cerr << "vix: --log-level=VALUE cannot be empty.\n";
                    return 1;
                }
                logLevelFlag = value;
                ++index;
                continue;
            }

            // Not a global option → this is the command
            break;
        }

        // 1.c Apply verbosity mode (overrides env base level)
        switch (verbosity)
        {
        case VerbosityMode::Verbose:
            logger.setLevel(Logger::Level::DEBUG);
            break;
        case VerbosityMode::Quiet:
            logger.setLevel(Logger::Level::WARN);
            break;
        case VerbosityMode::Default:
        default:
            // If env not set anything explicit, we keep whatever default
            // the Logger was initialized with (often INFO).
            break;
        }

        // 1.d Apply explicit --log-level if provided (highest precedence)
        if (logLevelFlag.has_value())
        {
            apply_log_level_from_flag(logger, *logLevelFlag);
        }

        // ----------------------------------------------------------
        // 2) Determine command + args
        // ----------------------------------------------------------
        if (index >= argc)
        {
            // Ex: vix --verbose (no command)
            return help({});
        }

        std::string cmd = argv[index];
        std::vector<std::string> args(argv + index + 1, argv + argc);

        // ----------------------------------------------------------
        // 3) Per-command help:
        //    vix <command> --help / -h
        // ----------------------------------------------------------
        if (!args.empty() && (args[0] == "--help" || args[0] == "-h"))
        {
            if (cmd == "new")
                return commands::NewCommand::help();
            if (cmd == "build")
                return commands::BuildCommand::help();
            if (cmd == "run")
                return commands::RunCommand::help();
            if (cmd == "dev")
                return commands::DevCommand::help();

            // Unknown command → global help
            std::cerr << "vix: unknown command '" << cmd << "'\n\n";
            return help({});
        }

        // ----------------------------------------------------------
        // 4) Dispatch normal command
        // ----------------------------------------------------------
        if (commands_.count(cmd))
        {
            try
            {
                return commands_[cmd](args);
            }
            catch (const std::exception &ex)
            {
                logger.logModule("CLI", Logger::Level::ERROR,
                                 "Command '{}' failed: {}", cmd, ex.what());
                return 1;
            }
        }

        std::cerr << "vix: unknown command '" << cmd << "'\n\n";
        help({});
        return 1;
    }

    // -----------------------------------------------------------------
    // CLI::help — global help + vix help <command>
    // -----------------------------------------------------------------
    int CLI::help(const std::vector<std::string> &args)
    {
        // Per-command help: vix help <command>
        if (!args.empty())
        {
            const std::string &cmd = args[0];
            if (cmd == "new")
                return commands::NewCommand::help();
            if (cmd == "build")
                return commands::BuildCommand::help();
            if (cmd == "run")
                return commands::RunCommand::help();
            if (cmd == "dev")
                return commands::DevCommand::help();
        }

#ifndef VIX_CLI_VERSION
#define VIX_CLI_VERSION "dev"
#endif

        std::ostream &out = std::cout;

        out << "Vix.cpp — Modern C++ backend runtime\n";
        out << "Version: " << VIX_CLI_VERSION << "\n\n";

        out << "Usage:\n";
        out << "  vix [GLOBAL OPTIONS] <COMMAND> [ARGS...]\n\n";

        out << "Commands:\n";
        out << "  Project:\n";
        out << "    new <name>             Scaffold a new Vix project in ./<name>\n\n";

        out << "  Development:\n";
        out << "    build [name]           Configure and build a project (root or app)\n";
        out << "    run [name] [--args]    Build (if needed) and run a project or app\n";
        out << "    dev [name]             Run in dev mode (auto-reload, rebuild-on-save)\n";
        out << "                           Options: --force-server, --force-script\n\n";

        out << "  Info:\n";
        out << "    help [command]         Show this help or help for a specific command\n";
        out << "    version                Show CLI version information\n\n";

        out << "Global options:\n";
        out << "  --verbose                Show debug logs from the runtime (log-level=debug)\n";
        out << "  -q, --quiet              Only show warnings and errors\n";
        out << "  --log-level <level>      Set log level: trace, debug, info, warn, error, critical\n";
        out << "                           (overrides VIX_LOG_LEVEL environment variable)\n";
        out << "  -h, --help               Show help for the CLI (or use: vix help)\n";
        out << "  -v, --version            Show version information\n\n";

        out << "Environment:\n";
        out << "  VIX_LOG_LEVEL=level      Default log level if no CLI override is provided.\n";
        out << "                           Accepted values: trace, debug, info, warn, error, critical.\n\n";

        out << "Examples:\n";
        out << "  vix new api\n";
        out << "  vix build                           # build current project\n";
        out << "  vix run api -- --port 8080\n";
        out << "\n";
        out << "  vix dev server.cpp                  # auto-detect server/script behaviour\n";
        out << "  vix dev server.cpp --force-server   # force server mode\n";
        out << "  vix dev tool.cpp --force-script     # force script mode\n\n";

        section_title(out, "Links:");
        out << "  GitHub: " << link("https://github.com/vixcpp/vix") << "\n\n";

        return 0;
    }

    // -----------------------------------------------------------------
    // CLI::version — simple version banner
    // -----------------------------------------------------------------
    int CLI::version(const std::vector<std::string> &)
    {
        using namespace vix::cli::style;

#ifndef VIX_CLI_VERSION
#define VIX_CLI_VERSION "dev"
#endif

        std::ostream &out = std::cout;

        // Titre coloré (cyan + bold), sans padding
        section_title(out, "Vix.cpp CLI");

        // Lignes d'infos alignées comme avant, mais stylées
        out << "  version : "
            << CYAN << VIX_CLI_VERSION << RESET << "\n";

        out << "  author  : "
            << "Gaspard Kirira\n";

        out << "  source  : "
            << link("https://github.com/vixcpp/vix") << "\n\n";

        return 0;
    }

} // namespace vix
