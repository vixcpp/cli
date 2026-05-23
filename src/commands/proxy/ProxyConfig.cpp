/**
 *
 *  @file ProxyConfig.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/proxy/ProxyConfig.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands::proxy
{
  namespace
  {
    std::string trim_copy(std::string value)
    {
      while (!value.empty() &&
             (value.back() == '\n' ||
              value.back() == '\r' ||
              value.back() == '\t' ||
              value.back() == ' '))
      {
        value.pop_back();
      }

      std::size_t index = 0;

      while (index < value.size() &&
             (value[index] == '\n' ||
              value[index] == '\r' ||
              value[index] == '\t' ||
              value[index] == ' '))
      {
        ++index;
      }

      return value.substr(index);
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

    json read_proxy_object(const json &root)
    {
      if (!root.is_object())
        return json::object();

      if (!root.contains("production") ||
          !root["production"].is_object())
      {
        return json::object();
      }

      const auto &production = root["production"];

      if (!production.contains("proxy") ||
          !production["proxy"].is_object())
      {
        return json::object();
      }

      return production["proxy"];
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

    fs::path default_certificate_path(const std::string &domain)
    {
      return fs::path("/etc/letsencrypt/live") / domain / "fullchain.pem";
    }

    fs::path default_certificate_key_path(const std::string &domain)
    {
      return fs::path("/etc/letsencrypt/live") / domain / "privkey.pem";
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

  std::filesystem::path nginx_sites_available_path(
      const NginxProxyConfig &cfg)
  {
    return fs::path("/etc/nginx/sites-available") / cfg.appName;
  }

  std::filesystem::path nginx_sites_enabled_path(
      const NginxProxyConfig &cfg)
  {
    return fs::path("/etc/nginx/sites-enabled") / cfg.appName;
  }

  NginxProxyConfig load_nginx_proxy_config()
  {
    NginxProxyConfig cfg;

    cfg.appName = read_project_name().value_or("vix-app");

    const json root = read_json_or_empty(fs::current_path() / "vix.json");
    const json proxy = read_proxy_object(root);

    if (auto domain = read_string(proxy, "domain"))
      cfg.domain = *domain;

    if (proxy.contains("http") && proxy["http"].is_object())
    {
      const auto &http = proxy["http"];

      if (auto port = read_int(http, "port"))
        cfg.httpPort = *port;
    }

    if (proxy.contains("websocket") && proxy["websocket"].is_object())
    {
      const auto &websocket = proxy["websocket"];

      if (auto enabled = read_bool(websocket, "enabled"))
        cfg.websocketEnabled = *enabled;

      if (auto path = read_string(websocket, "path"))
        cfg.websocketPath = normalize_path(*path);

      if (auto port = read_int(websocket, "port"))
        cfg.websocketPort = *port;
    }

    if (proxy.contains("tls") && proxy["tls"].is_object())
    {
      const auto &tls = proxy["tls"];

      if (auto enabled = read_bool(tls, "enabled"))
        cfg.tlsEnabled = *enabled;

      if (auto cert = read_string(tls, "certificate"))
        cfg.certificatePath = fs::path(*cert);

      if (auto key = read_string(tls, "certificate_key"))
        cfg.certificateKeyPath = fs::path(*key);
    }

    if (cfg.tlsEnabled && !cfg.domain.empty())
    {
      if (cfg.certificatePath.empty())
        cfg.certificatePath = default_certificate_path(cfg.domain);

      if (cfg.certificateKeyPath.empty())
        cfg.certificateKeyPath = default_certificate_key_path(cfg.domain);
    }

    cfg.sitesAvailablePath = nginx_sites_available_path(cfg);
    cfg.sitesEnabledPath = nginx_sites_enabled_path(cfg);

    return cfg;
  }
}
