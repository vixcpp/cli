/**
 *
 *  @file ProxyCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_PROXY_COMMAND_HPP
#define VIX_PROXY_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  /**
   * @brief Command entry point for reverse proxy management.
   */
  struct ProxyCommand
  {
    /**
     * @brief Run the proxy command.
     *
     * Supported form:
     *
     * `vix proxy nginx <init|check|reload>`
     *
     * @param args Command arguments after `proxy`.
     * @return Process exit code.
     */
    static int run(const std::vector<std::string> &args);

    /**
     * @brief Print proxy command help.
     *
     * @return Process exit code.
     */
    static int help();
  };
}

#endif
