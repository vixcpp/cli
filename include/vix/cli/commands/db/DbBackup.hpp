/**
 *
 *  @file DbBackup.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_DB_BACKUP_HPP
#define VIX_DB_BACKUP_HPP

#include <vix/cli/commands/db/DbTypes.hpp>

namespace vix::commands::db::backup
{
  /**
   * @brief Create a SQLite database backup.
   *
   * This function creates a timestamped backup copy of the configured
   * SQLite database file. If SQLite WAL and SHM files are present, they
   * are copied beside the database backup as well.
   *
   * Current MVP behavior:
   * - supports SQLite only
   * - requires the database file to exist
   * - creates the backups directory if missing
   * - copies `.db`, `.db-wal`, and `.db-shm` when present
   *
   * @param cfg Effective database configuration.
   * @param options Runtime database options.
   * @return Process exit code.
   */
  int create_backup(
      const DbConfig &cfg,
      const DbOptions &options);
}

#endif
