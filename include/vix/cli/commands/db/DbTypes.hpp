/**
 *
 *  @file DbTypes.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_DB_TYPES_HPP
#define VIX_DB_TYPES_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace vix::commands::db
{
  /**
   * @enum DbEngine
   * @brief Database engine detected for the current project.
   */
  enum class DbEngine
  {
    /**
     * @brief SQLite database engine.
     */
    SQLite,

    /**
     * @brief MySQL database engine.
     */
    MySQL,

    /**
     * @brief Unknown or unsupported database engine.
     */
    Unknown
  };

  /**
   * @enum DbStatus
   * @brief Overall database inspection status.
   */
  enum class DbStatus
  {
    /**
     * @brief Database configuration and storage look valid.
     */
    Ok,

    /**
     * @brief Database inspection found warnings.
     */
    Warning,

    /**
     * @brief Database inspection found blocking errors.
     */
    Error
  };

  /**
   * @struct DbOptions
   * @brief Runtime options passed from the db command line.
   */
  struct DbOptions
  {
    /**
     * @brief Print machine-readable JSON output when supported.
     */
    bool json{false};

    /**
     * @brief Enable verbose diagnostic output.
     */
    bool verbose{false};
  };

  /**
   * @struct DbConfig
   * @brief Database configuration resolved from the current Vix project.
   */
  struct DbConfig
  {
    /**
     * @brief Application or project name.
     */
    std::string appName{"vix-app"};

    /**
     * @brief Detected database engine.
     */
    DbEngine engine{DbEngine::SQLite};

    /**
     * @brief SQLite database file path.
     */
    std::filesystem::path databasePath{"storage/vix.db"};

    /**
     * @brief Storage directory containing the database file.
     */
    std::filesystem::path storageDir{"storage"};

    /**
     * @brief SQLite WAL file path.
     */
    std::filesystem::path walPath{};

    /**
     * @brief SQLite shared-memory file path.
     */
    std::filesystem::path shmPath{};

    /**
     * @brief Migrations directory path.
     */
    std::filesystem::path migrationsDir{"migrations"};
  };

  /**
   * @struct DbCheckResult
   * @brief Result produced by database storage inspection.
   */
  struct DbCheckResult
  {
    /**
     * @brief Effective database configuration used for inspection.
     */
    DbConfig config{};

    /**
     * @brief Overall status of the database inspection.
     */
    DbStatus status{DbStatus::Ok};

    /**
     * @brief Whether the configured database appears to use SQLite.
     */
    bool sqliteDetected{false};

    /**
     * @brief Whether the database file exists.
     */
    bool databaseExists{false};

    /**
     * @brief Whether the storage directory exists.
     */
    bool storageDirExists{false};

    /**
     * @brief Whether the storage directory is writable.
     */
    bool storageDirWritable{false};

    /**
     * @brief Whether the SQLite WAL file exists.
     */
    bool walFileExists{false};

    /**
     * @brief Whether the SQLite shared-memory file exists.
     */
    bool shmFileExists{false};

    /**
     * @brief Whether the migrations directory exists.
     */
    bool migrationsDirExists{false};

    /**
     * @brief Warning messages collected during inspection.
     */
    std::vector<std::string> warnings{};

    /**
     * @brief Error messages collected during inspection.
     */
    std::vector<std::string> errors{};
  };
}

#endif
