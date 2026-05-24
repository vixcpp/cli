/**
 *
 *  @file DbOutput.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/db/DbOutput.hpp>
#include <vix/cli/util/Ui.hpp>
#include <nlohmann/json.hpp>

#include <ostream>
#include <string>

using json = nlohmann::json;

namespace vix::commands::db::output
{
  namespace
  {
    std::string yes_no(bool value)
    {
      return value ? "yes" : "no";
    }

    std::string engine_name(DbEngine engine)
    {
      switch (engine)
      {
      case DbEngine::SQLite:
        return "sqlite";

      case DbEngine::MySQL:
        return "mysql";

      case DbEngine::Unknown:
        return "unknown";
      }

      return "unknown";
    }

    std::string status_name(DbStatus status)
    {
      switch (status)
      {
      case DbStatus::Ok:
        return "ok";

      case DbStatus::Warning:
        return "warning";

      case DbStatus::Error:
        return "error";
      }

      return "unknown";
    }
  }

  void print_status(
      std::ostream &out,
      const DbCheckResult &result,
      const DbOptions &options)
  {
    if (options.json)
    {
      print_status_json(out, result);
      return;
    }

    const DbConfig &cfg = result.config;

    vix::cli::util::section(out, "Database");

    vix::cli::util::kv(out, "Project", cfg.appName);
    vix::cli::util::kv(out, "Engine", engine_name(cfg.engine));
    vix::cli::util::kv(out, "Path", cfg.databasePath.string());
    vix::cli::util::kv(out, "Storage", cfg.storageDir.string());
    vix::cli::util::kv(out, "Database exists", yes_no(result.databaseExists));
    vix::cli::util::kv(out, "Storage exists", yes_no(result.storageDirExists));
    vix::cli::util::kv(out, "Storage writable", yes_no(result.storageDirWritable));

    vix::cli::util::section(out, "SQLite");

    vix::cli::util::kv(out, "Detected", yes_no(result.sqliteDetected));
    vix::cli::util::kv(out, "WAL path", cfg.walPath.string());
    vix::cli::util::kv(out, "WAL file", result.walFileExists ? "present" : "not present");
    vix::cli::util::kv(out, "SHM path", cfg.shmPath.string());
    vix::cli::util::kv(out, "SHM file", result.shmFileExists ? "present" : "not present");

    vix::cli::util::section(out, "Migrations");

    vix::cli::util::kv(out, "Directory", cfg.migrationsDir.string());
    vix::cli::util::kv(out, "Exists", yes_no(result.migrationsDirExists));

    vix::cli::util::section(out, "Status");

    vix::cli::util::kv(out, "Result", status_name(result.status));

    for (const std::string &message : result.warnings)
    {
      warn(out, message);
    }

    for (const std::string &message : result.errors)
    {
      error(out, message);
    }

    if (result.status == DbStatus::Ok)
    {
      ok(out, "database status looks good");
    }
  }

  void print_status_json(
      std::ostream &out,
      const DbCheckResult &result)
  {
    const DbConfig &cfg = result.config;

    json root = json::object();

    root["project"] = cfg.appName;
    root["engine"] = engine_name(cfg.engine);
    root["status"] = status_name(result.status);

    root["database"] = {
        {"path", cfg.databasePath.string()},
        {"exists", result.databaseExists},
    };

    root["storage"] = {
        {"path", cfg.storageDir.string()},
        {"exists", result.storageDirExists},
        {"writable", result.storageDirWritable},
    };

    root["sqlite"] = {
        {"detected", result.sqliteDetected},
        {"wal_path", cfg.walPath.string()},
        {"wal_exists", result.walFileExists},
        {"shm_path", cfg.shmPath.string()},
        {"shm_exists", result.shmFileExists},
    };

    root["migrations"] = {
        {"path", cfg.migrationsDir.string()},
        {"exists", result.migrationsDirExists},
    };

    root["warnings"] = json::array();

    for (const std::string &message : result.warnings)
    {
      root["warnings"].push_back(message);
    }

    root["errors"] = json::array();

    for (const std::string &message : result.errors)
    {
      root["errors"].push_back(message);
    }

    out << root.dump(2) << '\n';
  }

  void step(
      std::ostream &out,
      const std::string &label)
  {
    vix::cli::util::section(out, label);
  }

  void ok(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::ok_line(out, message);
  }

  void warn(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::warn_line(out, message);
  }

  void error(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::err_line(out, message);
  }

  void fix(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::warn_line(out, "Fix: " + message);
  }
}
