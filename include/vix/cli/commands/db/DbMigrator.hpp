/**
 *
 *  @file DbMigrator.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_DB_MIGRATOR_HPP
#define VIX_DB_MIGRATOR_HPP

#include <vix/cli/commands/db/DbTypes.hpp>

namespace vix::commands::db::migrator
{
  /**
   * @brief Apply pending database migrations.
   *
   * This function applies file-based SQL migrations from the configured
   * migrations directory.
   *
   * Current MVP behavior:
   * - supports SQLite only
   * - requires the storage directory to exist
   * - requires the migrations directory to exist
   * - creates the SQLite database file if it does not exist yet
   *
   * @param cfg Effective database configuration.
   * @param options Runtime database options.
   * @return Process exit code.
   */
  int migrate(
      const DbConfig &cfg,
      const DbOptions &options);
}

#endif
