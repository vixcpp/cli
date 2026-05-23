/**
 *
 *  @file HealthCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_HEALTH_COMMAND_HPP
#define VIX_HEALTH_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  /**
   * @brief Command entry point for production health checks.
   */
  struct HealthCommand
  {
    /**
     * @brief Run the health command.
     *
     * Supported forms:
     *
     * `vix health`
     * `vix health local`
     * `vix health public`
     * `vix health websocket`
     *
     * @param args Command arguments after `health`.
     * @return Process exit code.
     */
    static int run(const std::vector<std::string> &args);

    /**
     * @brief Print health command help.
     *
     * @return Process exit code.
     */
    static int help();
  };
}

#endif
