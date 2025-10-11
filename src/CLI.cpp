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
 * @version 0.1.0
 * @date 2025
 * @authors
 * SoftAdAstra
 */

#include <vix/cli/CLI.hpp>
#include <vix/cli/commands/NewCommand.hpp>
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/utils/Logger.hpp>

#include <iostream>
#include <unordered_map>

namespace Vix
{
    CLI::CLI()
    {
        // Base commands
        commands_["help"] = [this](auto args)
        { return help(args); };
        commands_["version"] = [this](auto args)
        { return version(args); };

        // Main CLI commands
        commands_["new"] = [](auto args)
        { return Commands::NewCommand::run(args); };
        commands_["run"] = [](auto args)
        { return Commands::RunCommand::run(args); };
        commands_["build"] = [](auto args)
        { return Commands::BuildCommand::run(args); };

        // Useful aliases
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

    int CLI::run(int argc, char **argv)
    {
        auto &logger = Logger::getInstance();

        if (argc < 2)
        {
            logger.log(Logger::Level::WARN, "Usage: vix <command> [options]");
            help({});
            return 1;
        }

        std::string cmd = argv[1];
        std::vector<std::string> args(argv + 2, argv + argc);

        if (commands_.count(cmd))
        {
            try
            {
                return commands_[cmd](args);
            }
            catch (const std::exception &ex)
            {
                logger.logModule("CLI", Logger::Level::ERROR, "Command '{}' failed: {}", cmd, ex.what());
                return 1;
            }
        }

        logger.logModule("CLI", Logger::Level::ERROR, "Unknown command: '{}'", cmd);
        help({});
        return 1;
    }

    int CLI::help(const std::vector<std::string> &)
    {
        auto &logger = Logger::getInstance();
        logger.log(Logger::Level::INFO, "");
        logger.log(Logger::Level::INFO, "✨ Vix.cpp CLI - Developer Commands ✨");
        logger.log(Logger::Level::INFO, "----------------------------------------");
        logger.log(Logger::Level::INFO, "  new <name>          Create a new Vix project");
        logger.log(Logger::Level::INFO, "  build [name]        Build a project (root or app)");
        logger.log(Logger::Level::INFO, "  run [name] [--args] Run a project or app");
        logger.log(Logger::Level::INFO, "  version             Show CLI version");
        logger.log(Logger::Level::INFO, "  help                Show this help message");
        logger.log(Logger::Level::INFO, "");
        logger.log(Logger::Level::INFO, "Examples:");
        logger.log(Logger::Level::INFO, "  vix new blog");
        logger.log(Logger::Level::INFO, "  vix build blog --config Debug");
        logger.log(Logger::Level::INFO, "  vix run blog -- --port 8080");
        logger.log(Logger::Level::INFO, "");
        return 0;
    }

    int CLI::version(const std::vector<std::string> &)
    {
        auto &logger = Logger::getInstance();
        logger.log(Logger::Level::INFO, "Vix.cpp CLI version 0.1.0");
        logger.log(Logger::Level::INFO, "Developed by SoftAdAstra");
        return 0;
    }
}
