/**
 *
 *  @file HealthConfig.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/health/HealthConfig.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands::health
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

    std::optional<std::uint64_t> read_uint64(
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

    json read_health_object(const json &root)
    {
      if (!root.is_object())
        return json::object();

      if (!root.contains("production") ||
          !root["production"].is_object())
      {
        return json::object();
      }

      const auto &production = root["production"];

      if (!production.contains("health") ||
          !production["health"].is_object())
      {
        return json::object();
      }

      return production["health"];
    }

    HealthEndpointConfig make_endpoint(
        const json &health,
        const std::string &key,
        int defaultExpectedStatus,
        bool applyExpectedStatus)
    {
      HealthEndpointConfig endpoint;
      endpoint.expectedStatus = defaultExpectedStatus;

      if (applyExpectedStatus)
      {
        if (auto expected = read_int(health, "expected_status"))
          endpoint.expectedStatus = *expected;
      }

      if (auto timeout = read_uint64(health, "timeout_ms"))
        endpoint.timeoutMs = *timeout;

      if (auto max = read_uint64(health, "max_response_ms"))
        endpoint.maxResponseMs = *max;

      if (auto url = read_string(health, key))
      {
        endpoint.enabled = true;
        endpoint.url = *url;
      }

      return endpoint;
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

  HealthConfig load_health_config()
  {
    HealthConfig cfg;

    cfg.appName = read_project_name().value_or("vix-app");

    const json root = read_json_or_empty(fs::current_path() / "vix.json");
    const json health = read_health_object(root);

    if (auto service = read_string(health, "service"))
      cfg.serviceName = *service;

    cfg.local = make_endpoint(health, "local", 200, true);
    cfg.publicEndpoint = make_endpoint(health, "public", 200, true);
    cfg.websocket = make_endpoint(health, "websocket", 101, false);

    return cfg;
  }

  const char *target_name(HealthTarget target) noexcept
  {
    switch (target)
    {
    case HealthTarget::Local:
      return "local";

    case HealthTarget::Public:
      return "public";

    case HealthTarget::WebSocket:
      return "websocket";
    }

    return "unknown";
  }
}
