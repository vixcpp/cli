/**
 *
 *  @file DbCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_DB_COMMAND_HPP
#define VIX_DB_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  /**
   * @brief Command entry point for database and storage inspection.
   */
  struct DbCommand
  {
    /**
     * @brief Run the db command.
     *
     * Supported forms:
     *
     * `vix db`
     * `vix db status`
     *
     * Options:
     *
     * `--json`
     * `--verbose`
     *
     * @param args Command arguments after `db`.
     * @return Process exit code.
     */
    static int run(const std::vector<std::string> &args);

    /**
     * @brief Print db command help.
     *
     * @return Process exit code.
     */
    static int help();
  };
}

#endif
