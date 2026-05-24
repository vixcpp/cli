/**
 *
 *  @file DbConfig.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/db/DbConfig.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands::db
{
  namespace
  {
    std::string trim_copy(std::string value)
    {
      auto not_space = [](unsigned char ch)
      {
        return !std::isspace(ch);
      };

      value.erase(
          value.begin(),
          std::find_if(value.begin(), value.end(), not_space));

      value.erase(
          std::find_if(value.rbegin(), value.rend(), not_space).base(),
          value.end());

      return value;
    }

    std::string to_lower_copy(std::string value)
    {
      std::transform(
          value.begin(),
          value.end(),
          value.begin(),
          [](unsigned char ch)
          {
            return static_cast<char>(std::tolower(ch));
          });

      return value;
    }

    json read_json_or_empty(const fs::path &path)
    {
      if (!fs::exists(path))
        return json::object();

      std::ifstream in(path);

      if (!in)
        return json::object();

      json value;

      try
      {
        in >> value;
      }
      catch (...)
      {
        return json::object();
      }

      if (!value.is_object())
        return json::object();

      return value;
    }

    std::optional<std::string> read_string(
        const json &object,
        const std::string &key)
    {
      if (!object.is_object() ||
          !object.contains(key) ||
          !object[key].is_string())
      {
        return std::nullopt;
      }

      const std::string value =
          trim_copy(object[key].get<std::string>());

      if (value.empty())
        return std::nullopt;

      return value;
    }

    json read_database_object(const json &root)
    {
      if (!root.is_object())
        return json::object();

      if (!root.contains("database") ||
          !root["database"].is_object())
      {
        return json::object();
      }

      return root["database"];
    }

    json read_sqlite_object(const json &database)
    {
      if (!database.is_object())
        return json::object();

      if (!database.contains("sqlite") ||
          !database["sqlite"].is_object())
      {
        return json::object();
      }

      return database["sqlite"];
    }

    DbEngine parse_engine(const std::string &value)
    {
      const std::string engine = to_lower_copy(trim_copy(value));

      if (engine == "sqlite")
        return DbEngine::SQLite;

      if (engine == "mysql")
        return DbEngine::MySQL;

      return DbEngine::Unknown;
    }

    fs::path default_database_path(const std::string &projectName)
    {
      return fs::path("storage") / (projectName + ".db");
    }

    fs::path default_storage_dir(const fs::path &databasePath)
    {
      if (databasePath.has_parent_path())
        return databasePath.parent_path();

      return fs::path("storage");
    }

    fs::path default_wal_path(const fs::path &databasePath)
    {
      return fs::path(databasePath.string() + "-wal");
    }

    fs::path default_shm_path(const fs::path &databasePath)
    {
      return fs::path(databasePath.string() + "-shm");
    }
  }

  std::optional<std::string> read_project_name()
  {
    const json root =
        read_json_or_empty(fs::current_path() / "vix.json");

    if (auto name = read_string(root, "name"))
      return name;

    for (const auto &entry : fs::directory_iterator(fs::current_path()))
    {
      if (!entry.is_regular_file())
        continue;

      if (entry.path().extension() == ".vix")
        return entry.path().stem().string();
    }

    const std::string fallback =
        trim_copy(fs::current_path().filename().string());

    if (!fallback.empty())
      return fallback;

    return std::nullopt;
  }

  DbConfig load_db_config()
  {
    DbConfig cfg;

    cfg.appName = read_project_name().value_or("vix-app");
    cfg.engine = DbEngine::SQLite;
    cfg.databasePath = default_database_path(cfg.appName);
    cfg.storageDir = default_storage_dir(cfg.databasePath);
    cfg.walPath = default_wal_path(cfg.databasePath);
    cfg.shmPath = default_shm_path(cfg.databasePath);
    cfg.migrationsDir = fs::path("migrations");

    const json root =
        read_json_or_empty(fs::current_path() / "vix.json");

    const json database =
        read_database_object(root);

    const json sqlite =
        read_sqlite_object(database);

    if (auto engine = read_string(database, "engine"))
      cfg.engine = parse_engine(*engine);

    if (auto sqlitePath = read_string(sqlite, "path"))
      cfg.databasePath = fs::path(*sqlitePath);
    else if (auto flatSqlitePath = read_string(database, "sqlite_path"))
      cfg.databasePath = fs::path(*flatSqlitePath);

    if (auto storage = read_string(database, "storage"))
      cfg.storageDir = fs::path(*storage);
    else
      cfg.storageDir = default_storage_dir(cfg.databasePath);

    if (auto migrations = read_string(database, "migrations"))
      cfg.migrationsDir = fs::path(*migrations);

    cfg.walPath = default_wal_path(cfg.databasePath);
    cfg.shmPath = default_shm_path(cfg.databasePath);

    return cfg;
  }

  DbConfig apply_db_options(
      DbConfig cfg,
      const DbOptions &options)
  {
    (void)options;
    return cfg;
  }
}
