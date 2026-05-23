/**
 *
 *  @file LogsCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_LOGS_COMMAND_HPP
#define VIX_LOGS_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  /**
   * @brief Command entry point for production logs management.
   */
  struct LogsCommand
  {
    /**
     * @brief Run the logs command.
     *
     * Supported forms:
     *
     * `vix logs`
     * `vix logs app`
     * `vix logs proxy`
     * `vix logs errors`
     *
     * Options:
     *
     * `--follow`
     * `--errors`
     * `--since <value>`
     * `--lines <n>`
     * `-n <n>`
     *
     * @param args Command arguments after `logs`.
     * @return Process exit code.
     */
    static int run(const std::vector<std::string> &args);

    /**
     * @brief Print logs command help.
     *
     * @return Process exit code.
     */
    static int help();
  };
}

#endif
