/**
 *
 *  @file CLI.hpp
 *  @author Gaspard Kirira
 *
 *  @brief Declares the main command-line interface entry point for Vix.cpp.
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
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
   * @brief Main command-line interface dispatcher for Vix.cpp.
   *
   * The CLI class owns the command registry used by the executable entry point.
   * It receives raw command-line arguments, resolves the requested command, and
   * forwards execution to the matching command handler.
   */
  class CLI
  {
  public:
    /**
     * @brief Creates a CLI instance and registers the built-in commands.
     */
    CLI();

    /**
     * @brief Runs the command-line interface.
     *
     * This method parses the command-line arguments, selects the requested
     * command, and executes its registered handler.
     *
     * @param argc Number of command-line arguments.
     * @param argv Command-line argument values.
     *
     * @return Process exit code.
     */
    int run(int argc, char **argv);

  private:
    /**
     * @brief Function type used by CLI command handlers.
     *
     * A command handler receives the arguments that belong to the selected
     * command and returns the process exit code for that command.
     */
    using CommandHandler = std::function<int(const std::vector<std::string> &)>;

    /**
     * @brief Registered command handlers indexed by command name.
     */
    std::unordered_map<std::string, CommandHandler> commands_;

    /**
     * @brief Prints CLI help information.
     *
     * @param args Arguments passed to the help command.
     *
     * @return Process exit code.
     */
    int help(const std::vector<std::string> &args);

    /**
     * @brief Prints the Vix.cpp CLI version.
     *
     * @param args Arguments passed to the version command.
     *
     * @return Process exit code.
     */
    int version(const std::vector<std::string> &args);
  };
}

#endif
