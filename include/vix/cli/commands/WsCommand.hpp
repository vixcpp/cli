/**
 *
 *  @file WsCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_WS_COMMAND_HPP
#define VIX_WS_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  /**
   * @brief Command entry point for WebSocket production diagnostics.
   */
  struct WsCommand
  {
    /**
     * @brief Run the ws command.
     *
     * Supported forms:
     *
     * `vix ws check`
     * `vix ws check ws://127.0.0.1:9090/ws`
     *
     * Options:
     *
     * `--timeout <ms>`
     * `--no-ping`
     * `--verbose`
     *
     * @param args Command arguments after `ws`.
     * @return Process exit code.
     */
    static int run(const std::vector<std::string> &args);

    /**
     * @brief Print ws command help.
     *
     * @return Process exit code.
     */
    static int help();
  };
}

#endif
