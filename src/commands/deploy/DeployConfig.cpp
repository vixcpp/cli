/**
 *
 *  @file DeployConfig.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/deploy/DeployConfig.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands::deploy
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

      const std::string value = trim_copy(object[key].get<std::string>());

      if (value.empty())
        return std::nullopt;

      return value;
    }

    std::optional<bool> read_bool(
        const json &object,
        const std::string &key)
    {
      if (!object.is_object() ||
          !object.contains(key) ||
          !object[key].is_boolean())
      {
        return std::nullopt;
      }

      return object[key].get<bool>();
    }

    std::optional<int> read_int(
        const json &object,
        const std::string &key)
    {
      if (!object.is_object() ||
          !object.contains(key) ||
          !object[key].is_number_integer())
      {
        return std::nullopt;
      }

      return object[key].get<int>();
    }

    json read_deploy_object(const json &root)
    {
      if (!root.is_object())
        return json::object();

      if (!root.contains("production") ||
          !root["production"].is_object())
      {
        return json::object();
      }

      const auto &production = root["production"];

      if (!production.contains("deploy") ||
          !production["deploy"].is_object())
      {
        return json::object();
      }

      return production["deploy"];
    }

    std::string normalize_service_name(std::string value)
    {
      value = trim_copy(value);

      if (value.size() > 8 &&
          value.substr(value.size() - 8) == ".service")
      {
        value.erase(value.size() - 8);
      }

      return value;
    }
  }

  std::optional<std::string> read_project_name()
  {
    const json root = read_json_or_empty(fs::current_path() / "vix.json");

    if (auto name = read_string(root, "name"))
      return name;

    for (const auto &entry : fs::directory_iterator(fs::current_path()))
    {
      if (!entry.is_regular_file())
        continue;

      if (entry.path().extension() == ".vix")
        return entry.path().stem().string();
    }

    const std::string fallback = trim_copy(fs::current_path().filename().string());

    if (!fallback.empty())
      return fallback;

    return std::nullopt;
  }

  DeployConfig load_deploy_config()
  {
    DeployConfig cfg;

    cfg.appName = read_project_name().value_or("vix-app");
    cfg.serviceName = cfg.appName;

    const json root = read_json_or_empty(fs::current_path() / "vix.json");
    const json deploy = read_deploy_object(root);

    if (auto pull = read_bool(deploy, "pull"))
      cfg.pull = *pull;

    if (auto branch = read_string(deploy, "branch"))
      cfg.branch = *branch;

    if (auto build = read_string(deploy, "build"))
      cfg.buildCommand = *build;

    if (auto tests = read_bool(deploy, "tests"))
      cfg.tests = *tests;

    if (auto testCommand = read_string(deploy, "test_command"))
      cfg.testCommand = *testCommand;

    if (auto service = read_string(deploy, "service"))
      cfg.serviceName = normalize_service_name(*service);

    if (auto healthLocal = read_bool(deploy, "health_local"))
      cfg.healthLocal = *healthLocal;

    if (auto healthPublic = read_bool(deploy, "health_public"))
      cfg.healthPublic = *healthPublic;

    if (auto proxyCheck = read_bool(deploy, "proxy_check"))
      cfg.proxyCheck = *proxyCheck;

    if (auto proxyReload = read_bool(deploy, "proxy_reload"))
      cfg.proxyReload = *proxyReload;

    if (auto logs = read_bool(deploy, "logs_on_failure"))
      cfg.logsOnFailure = *logs;

    if (auto lines = read_int(deploy, "log_lines"))
      cfg.logLines = *lines;

    if (cfg.logLines <= 0)
      cfg.logLines = 80;

    return cfg;
  }

  DeployConfig apply_deploy_options(
      DeployConfig cfg,
      const DeployOptions &options)
  {
    if (options.noPull)
      cfg.pull = false;

    if (options.noTests)
      cfg.tests = false;

    return cfg;
  }
}
