/**
 *
 *  @file WsConfig.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/ws/WsConfig.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands::ws
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

    std::optional<std::uint64_t> read_u64(
        const json &object,
        const std::string &key)
    {
      if (!object.is_object() ||
          !object.contains(key) ||
          !object[key].is_number_unsigned())
      {
        return std::nullopt;
      }

      return object[key].get<std::uint64_t>();
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

    json read_ws_object(const json &production)
    {
      if (!production.is_object())
        return json::object();

      if (!production.contains("websocket") ||
          !production["websocket"].is_object())
      {
        return json::object();
      }

      return production["websocket"];
    }

    json read_proxy_object(const json &production)
    {
      if (!production.is_object())
        return json::object();

      if (!production.contains("proxy") ||
          !production["proxy"].is_object())
      {
        return json::object();
      }

      return production["proxy"];
    }

    json read_proxy_ws_object(const json &proxy)
    {
      if (!proxy.is_object())
        return json::object();

      if (!proxy.contains("websocket") ||
          !proxy["websocket"].is_object())
      {
        return json::object();
      }

      return proxy["websocket"];
    }

    std::string normalize_path(std::string value)
    {
      value = trim_copy(value);

      if (value.empty())
        return "/ws";

      if (value.front() != '/')
        value.insert(value.begin(), '/');

      return value;
    }

    std::string build_local_url(
        const std::string &host,
        int port,
        const std::string &path)
    {
      return "ws://" +
             host +
             ":" +
             std::to_string(port) +
             normalize_path(path);
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

  WsConfig load_ws_config()
  {
    WsConfig cfg;

    cfg.appName = read_project_name().value_or("vix-app");

    const json root = read_json_or_empty(fs::current_path() / "vix.json");
    const json production = read_production_object(root);
    const json ws = read_ws_object(production);
    const json proxy = read_proxy_object(production);
    const json proxyWs = read_proxy_ws_object(proxy);

    if (auto host = read_string(ws, "host"))
      cfg.host = *host;

    if (auto port = read_int(ws, "port"))
      cfg.port = *port;

    if (auto path = read_string(ws, "path"))
      cfg.path = normalize_path(*path);

    if (auto localUrl = read_string(ws, "local_url"))
      cfg.localUrl = *localUrl;

    if (auto publicUrl = read_string(ws, "public_url"))
      cfg.publicUrl = *publicUrl;

    if (auto timeout = read_u64(ws, "timeout_ms"))
      cfg.timeoutMs = *timeout;

    if (auto heartbeat = read_bool(ws, "heartbeat"))
      cfg.heartbeat = *heartbeat;

    if (auto proxyPort = read_int(proxyWs, "port"))
      cfg.port = *proxyPort;

    if (auto proxyPath = read_string(proxyWs, "path"))
      cfg.path = normalize_path(*proxyPath);

    if (cfg.port <= 0 || cfg.port > 65535)
      cfg.port = 9090;

    if (cfg.timeoutMs == 0)
      cfg.timeoutMs = 3000;

    if (cfg.localUrl.empty())
      cfg.localUrl = build_local_url(cfg.host, cfg.port, cfg.path);

    return cfg;
  }

  WsConfig apply_ws_options(
      WsConfig cfg,
      const WsOptions &options)
  {
    if (!options.url.empty())
    {
      cfg.localUrl = options.url;
    }

    if (options.timeoutMs > 0)
      cfg.timeoutMs = options.timeoutMs;

    return cfg;
  }
}
