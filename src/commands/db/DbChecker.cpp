/**
 *
 *  @file DbChecker.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/db/DbChecker.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace vix::commands::db::checker
{
  namespace
  {
    void add_warning(
        DbCheckResult &result,
        const std::string &message)
    {
      result.warnings.push_back(message);

      if (result.status == DbStatus::Ok)
        result.status = DbStatus::Warning;
    }

    void add_error(
        DbCheckResult &result,
        const std::string &message)
    {
      result.errors.push_back(message);
      result.status = DbStatus::Error;
    }

    bool can_write_directory(const fs::path &dir)
    {
      if (!fs::exists(dir) || !fs::is_directory(dir))
        return false;

      const fs::path probe =
          dir / ".vix-db-write-test";

      {
        std::ofstream out(probe);

        if (!out)
          return false;

        out << "ok";
      }

      std::error_code ec;
      fs::remove(probe, ec);

      return true;
    }

    bool has_sqlite_extension(const fs::path &path)
    {
      const std::string ext = path.extension().string();

      return ext == ".db" ||
             ext == ".sqlite" ||
             ext == ".sqlite3";
    }

    bool looks_like_sqlite_config(const DbConfig &cfg)
    {
      if (cfg.engine == DbEngine::SQLite)
        return true;

      if (has_sqlite_extension(cfg.databasePath))
        return true;

      return false;
    }

    void check_storage_directory(DbCheckResult &result)
    {
      const fs::path &storageDir =
          result.config.storageDir;

      result.storageDirExists =
          fs::exists(storageDir) &&
          fs::is_directory(storageDir);

      if (!result.storageDirExists)
      {
        add_error(
            result,
            "storage directory is missing: " + storageDir.string());

        return;
      }

      result.storageDirWritable =
          can_write_directory(storageDir);

      if (!result.storageDirWritable)
      {
        add_error(
            result,
            "storage directory is not writable: " + storageDir.string());
      }
    }

    void check_database_file(DbCheckResult &result)
    {
      const fs::path &databasePath =
          result.config.databasePath;

      result.databaseExists =
          fs::exists(databasePath) &&
          fs::is_regular_file(databasePath);

      if (!result.databaseExists)
      {
        add_warning(
            result,
            "database file does not exist yet: " + databasePath.string());
      }
    }

    void check_wal_files(DbCheckResult &result)
    {
      result.walFileExists =
          fs::exists(result.config.walPath) &&
          fs::is_regular_file(result.config.walPath);

      result.shmFileExists =
          fs::exists(result.config.shmPath) &&
          fs::is_regular_file(result.config.shmPath);
    }

    void check_migrations_directory(DbCheckResult &result)
    {
      const fs::path &migrationsDir =
          result.config.migrationsDir;

      result.migrationsDirExists =
          fs::exists(migrationsDir) &&
          fs::is_directory(migrationsDir);

      if (!result.migrationsDirExists)
      {
        add_warning(
            result,
            "migrations directory not found: " + migrationsDir.string());
      }
    }
  }

  DbCheckResult check_status(const DbConfig &cfg)
  {
    DbCheckResult result;
    result.config = cfg;

    result.sqliteDetected =
        looks_like_sqlite_config(cfg);

    if (cfg.engine == DbEngine::Unknown)
    {
      add_error(
          result,
          "unknown database engine in configuration");
    }

    if (!result.sqliteDetected)
    {
      add_warning(
          result,
          "SQLite usage was not detected for this project");

      check_migrations_directory(result);
      return result;
    }

    check_storage_directory(result);
    check_database_file(result);
    check_wal_files(result);
    check_migrations_directory(result);

    return result;
  }
}
