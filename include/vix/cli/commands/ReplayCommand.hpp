/**
 *
 *  @file ReplayCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_CLI_COMMANDS_REPLAY_COMMAND_HPP
#define VIX_CLI_COMMANDS_REPLAY_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::ReplayCommand
{

  /**
   * @brief Run the `vix replay` command.
   *
   * Supported forms:
   * - vix replay
   * - vix replay last
   * - vix replay latest
   * - vix replay failed
   * - vix replay <id>
   * - vix replay list
   * - vix replay list --failed
   * - vix replay show <id>
   * - vix replay clean
   *
   * @param args Command arguments after `replay`.
   * @return Process exit code.
   */
  int run(const std::vector<std::string> &args);

  /**
   * @brief Print help for the `vix replay` command.
   *
   * @return Process exit code.
   */
  int help();

} // namespace vix::commands::ReplayCommand

#endif // VIX_CLI_COMMANDS_REPLAY_COMMAND_HPP
