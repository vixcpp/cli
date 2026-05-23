/**
 *
 *  @file DeployCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_DEPLOY_COMMAND_HPP
#define VIX_DEPLOY_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  /**
   * @brief Command entry point for production deployment.
   */
  struct DeployCommand
  {
    /**
     * @brief Run the deploy command.
     *
     * Supported forms:
     *
     * `vix deploy`
     * `vix deploy --dry-run`
     * `vix deploy --verbose`
     * `vix deploy --no-pull`
     * `vix deploy --no-tests`
     *
     * @param args Command arguments after `deploy`.
     * @return Process exit code.
     */
    static int run(const std::vector<std::string> &args);

    /**
     * @brief Print deploy command help.
     *
     * @return Process exit code.
     */
    static int help();
  };
}

#endif
