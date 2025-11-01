/**
 * @file CLI.hpp
 * @brief Declaration of the Vix.cpp Command Line Interface (CLI) class.
 *
 * The `Vix::CLI` class provides the entry point for interacting with the Vix.cpp
 * framework via command line. It manages the registration and execution of
 * developer commands such as project creation, building, and running applications.
 *
 * ## Overview
 * The CLI parses user input, maps commands to corresponding handlers,
 * and executes them dynamically. It supports built-in commands (`help`, `version`)
 * as well as modular commands like `new`, `build`, and `run`.
 *
 * ## Key Responsibilities
 * - Register and manage available CLI commands.
 * - Parse and dispatch command-line arguments.
 * - Handle errors gracefully and log them through the global `Logger`.
 *
 * ## Example
 * ```bash
 * vix new myapp
 * vix build myapp --config Release
 * vix run myapp -- --port 8080
 * ```
 *
 * @note
 * This class is lightweight and self-contained.
 * Command handlers are defined in separate modules (e.g. `NewCommand`, `RunCommand`).
 *
 * @namespace Vix
 * @class CLI
 * @version 0.1.0
 * @date 2025
 * @authors
 * SoftAdAstra
 */

#ifndef VIX_CLI_HPP
#define VIX_CLI_HPP

#include <string>
#include <unordered_map>
#include <functional>
#include <iostream>
#include <vector>

namespace vix
{
    /**
     * @class CLI
     * @brief Core class implementing the Vix.cpp command-line interface.
     *
     * The CLI acts as the main command dispatcher. It holds a map of command names
     * and associated function handlers, enabling modular and extensible command logic.
     */
    class CLI
    {
    public:
        /**
         * @brief Constructs the CLI and registers all available commands.
         *
         * Initializes default commands (`help`, `version`) as well as
         * user-facing commands (`new`, `build`, `run`) and helpful aliases.
         */
        CLI();

        /**
         * @brief Entry point for executing a CLI command.
         *
         * Parses the provided command-line arguments, matches the command name,
         * and executes the corresponding handler.
         *
         * @param argc Number of command-line arguments.
         * @param argv Array of argument strings.
         * @return Exit code (0 for success, non-zero for error).
         *
         * @note Displays help automatically if no arguments are provided.
         */
        int run(int argc, char **argv);

    private:
        /// Type alias for command handler functions.
        using CommandHandler = std::function<int(const std::vector<std::string> &)>;

        /// Internal registry mapping command names to their handlers.
        std::unordered_map<std::string, CommandHandler> commands_;

        /**
         * @brief Displays all available commands and usage examples.
         *
         * Called when the user runs `vix help` or uses `-h` / `--help`.
         *
         * @param args Optional arguments (ignored).
         * @return Always returns 0.
         */
        int help(const std::vector<std::string> &args);

        /**
         * @brief Prints the current CLI version and author information.
         *
         * Called when the user runs `vix version` or uses `-v` / `--version`.
         *
         * @param args Optional arguments (ignored).
         * @return Always returns 0.
         */
        int version(const std::vector<std::string> &args);
    };
}

#endif
