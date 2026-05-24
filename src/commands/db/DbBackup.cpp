/**
 *
 *  @file DbBackup.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/db/DbBackup.hpp>
#include <vix/cli/commands/db/DbOutput.hpp>
#include <vix/cli/util/Ui.hpp>

#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace vix::commands::db::backup
{
  namespace
  {
    std::string timestamp()
    {
      using namespace std::chrono;

      const auto now = system_clock::now();
      const std::time_t t = system_clock::to_time_t(now);

      std::tm tm{};

#if defined(_WIN32)
      localtime_s(&tm, &t);
#else
      localtime_r(&t, &tm);
#endif

      std::ostringstream out;
      out << std::put_time(&tm, "%Y%m%d-%H%M%S");
      return out.str();
    }

    fs::path backup_database_path(
        const DbConfig &cfg,
        const std::string &stamp)
    {
      const std::string stem =
          cfg.databasePath.stem().string().empty()
              ? cfg.appName
              : cfg.databasePath.stem().string();

      const std::string ext =
          cfg.databasePath.extension().string().empty()
              ? ".db"
              : cfg.databasePath.extension().string();

      return fs::path("backups") / (stem + "-" + stamp + ext);
    }

    bool is_sqlite(const DbConfig &cfg)
    {
      return cfg.engine == DbEngine::SQLite;
    }

    bool copy_if_present(
        const fs::path &from,
        const fs::path &to)
    {
      if (!fs::exists(from) || !fs::is_regular_file(from))
        return false;

      fs::copy_file(
          from,
          to,
          fs::copy_options::overwrite_existing);

      return true;
    }

    fs::path sidecar_backup_path(
        const fs::path &backupPath,
        const std::string &suffix)
    {
      return fs::path(backupPath.string() + suffix);
    }
  }

  int create_backup(
      const DbConfig &cfg,
      const DbOptions &options)
  {
    (void)options;

    if (!is_sqlite(cfg))
    {
      output::error(
          std::cerr,
          "vix db backup currently supports SQLite only.");

      output::fix(
          std::cerr,
          "set database.engine to sqlite before using vix db backup");

      return 1;
    }

    if (!fs::exists(cfg.databasePath) ||
        !fs::is_regular_file(cfg.databasePath))
    {
      output::error(
          std::cerr,
          "database file not found: " + cfg.databasePath.string());

      output::fix(
          std::cerr,
          "run vix db migrate or create the database before backing it up");

      return 1;
    }

    try
    {
      const fs::path backupDir = "backups";
      fs::create_directories(backupDir);

      const std::string stamp = timestamp();
      const fs::path backupPath =
          backup_database_path(cfg, stamp);

      fs::copy_file(
          cfg.databasePath,
          backupPath,
          fs::copy_options::overwrite_existing);

      const bool copiedWal =
          copy_if_present(
              cfg.walPath,
              sidecar_backup_path(backupPath, "-wal"));

      const bool copiedShm =
          copy_if_present(
              cfg.shmPath,
              sidecar_backup_path(backupPath, "-shm"));

      output::step(std::cout, "Database Backup");

      vix::cli::util::kv(
          std::cout,
          "Database",
          cfg.databasePath.string());

      vix::cli::util::kv(
          std::cout,
          "Backup",
          backupPath.string());

      vix::cli::util::kv(
          std::cout,
          "WAL copied",
          copiedWal ? "yes" : "no");

      vix::cli::util::kv(
          std::cout,
          "SHM copied",
          copiedShm ? "yes" : "no");

      output::ok(
          std::cout,
          "database backup created");

      return 0;
    }
    catch (const std::exception &e)
    {
      output::error(
          std::cerr,
          "backup failed: " + std::string(e.what()));

      return 1;
    }
  }
}
