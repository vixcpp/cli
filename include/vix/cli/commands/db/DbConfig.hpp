/**
 *
 *  @file DbConfig.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_DB_CONFIG_HPP
#define VIX_DB_CONFIG_HPP

#include <vix/cli/commands/db/DbTypes.hpp>

#include <optional>
#include <string>

namespace vix::commands::db
{
  /**
   * @brief Read the current Vix project name.
   *
   * The project name is resolved from `vix.json` when available.
   * If no project name is found, the implementation may fall back to
   * the current directory name.
   *
   * @return The detected project name, or std::nullopt if no name can be resolved.
   */
  std::optional<std::string> read_project_name();

  /**
   * @brief Load database configuration from vix.json.
   *
   * The configuration is read from the `database` object when available.
   *
   * Supported keys:
   * - `database.engine`
   * - `database.sqlite.path`
   * - `database.storage`
   * - `database.migrations`
   *
   * Fallbacks:
   * - engine defaults to SQLite
   * - SQLite path defaults to `storage/<project>.db`
   * - storage directory defaults to the database parent directory
   * - migrations directory defaults to `migrations`
   *
   * @return Loaded database configuration.
   */
  DbConfig load_db_config();

  /**
   * @brief Apply command-line database options to the loaded config.
   *
   * This function currently keeps the configuration unchanged, but it exists
   * to keep the db command architecture consistent with other Vix commands.
   *
   * @param cfg Loaded database configuration.
   * @param options Runtime database options.
   * @return Effective database configuration.
   */
  DbConfig apply_db_options(
      DbConfig cfg,
      const DbOptions &options);
}

#endif
