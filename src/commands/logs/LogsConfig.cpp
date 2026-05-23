/**
 *
 *  @file LogsConfig.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/logs/LogsConfig.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands::logs
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

    json read_production_object(const json &root)
    {
      if (!root.is_object())
        return json::object();

      if (!root.contains("production") ||
          !root["production"].is_object())
      {
        return json::object();
      }

      return root["production"];
    }

    json read_logs_object(const json &production)
    {
      if (!production.is_object())
        return json::object();

      if (!production.contains("logs") ||
          !production["logs"].is_object())
      {
        return json::object();
      }

      return production["logs"];
    }

    json read_deploy_object(const json &production)
    {
      if (!production.is_object())
        return json::object();

      if (!production.contains("deploy") ||
          !production["deploy"].is_object())
      {
        return json::object();
      }

      return production["deploy"];
    }

    json read_service_object(const json &production)
    {
      if (!production.is_object())
        return json::object();

      if (!production.contains("service") ||
          !production["service"].is_object())
      {
        return json::object();
      }

      return production["service"];
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

    fs::path default_nginx_access_log(const std::string &appName)
    {
      return fs::path("/var/log/nginx") / (appName + ".access.log");
    }

    fs::path default_nginx_error_log(const std::string &appName)
    {
      return fs::path("/var/log/nginx") / (appName + ".error.log");
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

  LogsConfig load_logs_config()
  {
    LogsConfig cfg;

    cfg.appName = read_project_name().value_or("vix-app");
    cfg.serviceName = cfg.appName;
    cfg.nginxAccessLog = default_nginx_access_log(cfg.appName);
    cfg.nginxErrorLog = default_nginx_error_log(cfg.appName);

    const json root = read_json_or_empty(fs::current_path() / "vix.json");
    const json production = read_production_object(root);
    const json logs = read_logs_object(production);
    const json deploy = read_deploy_object(production);
    const json service = read_service_object(production);

    if (auto serviceName = read_string(logs, "service"))
      cfg.serviceName = normalize_service_name(*serviceName);
    else if (auto deployService = read_string(deploy, "service"))
      cfg.serviceName = normalize_service_name(*deployService);
    else if (auto serviceNameFromService = read_string(service, "name"))
      cfg.serviceName = normalize_service_name(*serviceNameFromService);

    if (auto accessLog = read_string(logs, "nginx_access"))
      cfg.nginxAccessLog = fs::path(*accessLog);

    if (auto errorLog = read_string(logs, "nginx_error"))
      cfg.nginxErrorLog = fs::path(*errorLog);

    if (auto lines = read_int(logs, "lines"))
      cfg.lines = *lines;

    if (cfg.lines <= 0)
      cfg.lines = 120;

    if (cfg.serviceName.empty())
      cfg.serviceName = cfg.appName;

    return cfg;
  }

  LogsConfig apply_logs_options(
      LogsConfig cfg,
      const LogsOptions &options)
  {
    if (options.lines > 0)
      cfg.lines = options.lines;

    return cfg;
  }
}
