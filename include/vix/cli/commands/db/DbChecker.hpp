/**
 *
 *  @file DbChecker.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_DB_CHECKER_HPP
#define VIX_DB_CHECKER_HPP

#include <vix/cli/commands/db/DbTypes.hpp>

namespace vix::commands::db::checker
{
  /**
   * @brief Inspect the current database storage state.
   *
   * This function checks the resolved database configuration and inspects
   * the filesystem for the SQLite database file, WAL file, SHM file,
   * storage directory, storage permissions, and migrations directory.
   *
   * It does not execute migrations and does not modify the database.
   *
   * @param cfg Effective database configuration.
   * @return Database inspection result.
   */
  DbCheckResult check_status(const DbConfig &cfg);
}

#endif
