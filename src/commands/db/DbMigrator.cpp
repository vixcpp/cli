/**
 *
 *  @file DbMigrator.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/db/DbMigrator.hpp>
#include <vix/cli/commands/db/DbOutput.hpp>
#include <vix/cli/util/Ui.hpp>

#ifdef VIX_CLI_HAS_DB
#include <vix/db/Database.hpp>
#include <vix/db/mig/FileMigrationsRunner.hpp>
#endif

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace vix::commands::db::migrator
{
  namespace
  {
    bool is_sqlite(const DbConfig &cfg)
    {
      return cfg.engine == DbEngine::SQLite;
    }

    bool validate_migration_inputs(const DbConfig &cfg)
    {
      if (!is_sqlite(cfg))
      {
        output::error(
            std::cerr,
            "vix db migrate currently supports SQLite only.");

        output::fix(
            std::cerr,
            "set database.engine to sqlite");

        return false;
      }

      if (!fs::exists(cfg.storageDir) ||
          !fs::is_directory(cfg.storageDir))
      {
        output::error(
            std::cerr,
            "storage directory is missing: " + cfg.storageDir.string());

        output::fix(
            std::cerr,
            "create the storage directory before running migrations");

        return false;
      }

      if (!fs::exists(cfg.migrationsDir) ||
          !fs::is_directory(cfg.migrationsDir))
      {
        output::error(
            std::cerr,
            "migrations directory is missing: " + cfg.migrationsDir.string());

        output::fix(
            std::cerr,
            "create the migrations directory and add .up.sql migration files");

        return false;
      }

      return true;
    }

    void print_migration_summary(const DbConfig &cfg)
    {
      output::step(std::cout, "Database Migrations");

      vix::cli::util::kv(std::cout, "Engine", "sqlite");
      vix::cli::util::kv(std::cout, "Database", cfg.databasePath.string());
      vix::cli::util::kv(std::cout, "Directory", cfg.migrationsDir.string());
    }
  }

  int migrate(
      const DbConfig &cfg,
      const DbOptions &options)
  {
    (void)options;

#ifndef VIX_CLI_HAS_DB
    output::error(
        std::cerr,
        "vix db migrate is not available in this build.");

    output::fix(
        std::cerr,
        "rebuild the Vix CLI with the db module enabled");

    return 1;
#else
    if (!validate_migration_inputs(cfg))
      return 1;

    print_migration_summary(cfg);

    try
    {
      vix::db::Database database =
          vix::db::Database::sqlite(cfg.databasePath.string());

      vix::db::PooledConn conn(database.pool());

      vix::db::FileMigrationsRunner runner(
          conn.get(),
          cfg.migrationsDir);

      runner.applyAll();

      output::ok(
          std::cout,
          "migrations applied successfully");

      return 0;
    }
    catch (const std::exception &e)
    {
      output::error(
          std::cerr,
          "migration failed: " + std::string(e.what()));

      return 1;
    }
#endif
  }
}
