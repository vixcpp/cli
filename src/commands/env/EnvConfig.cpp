/**
 *
 *  @file EnvConfig.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/env/EnvConfig.hpp>
#include <vix/utils/Env.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands::env
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

    std::string lower_copy(std::string value)
    {
      std::transform(
          value.begin(),
          value.end(),
          value.begin(),
          [](unsigned char c)
          {
            return static_cast<char>(std::tolower(c));
          });

      return value;
    }

    bool starts_with(const std::string &value, const std::string &prefix)
    {
      return value.size() >= prefix.size() &&
             value.compare(0, prefix.size(), prefix) == 0;
    }

    std::string unquote_value(std::string value)
    {
      value = trim_copy(std::move(value));

      if (value.size() >= 2)
      {
        const char first = value.front();
        const char last = value.back();

        if ((first == '"' && last == '"') ||
            (first == '\'' && last == '\''))
        {
          value = value.substr(1, value.size() - 2);
        }
      }

      return value;
    }

    std::optional<json> read_json_file(const fs::path &path)
    {
      std::ifstream in(path);

      if (!in)
        return std::nullopt;

      try
      {
        json root;
        in >> root;
        return root;
      }
      catch (...)
      {
        return std::nullopt;
      }
    }

    std::optional<std::string> read_project_name()
    {
      const fs::path path = fs::current_path() / "vix.json";
      const auto root = read_json_file(path);

      if (!root ||
          !root->is_object() ||
          !root->contains("name") ||
          !(*root)["name"].is_string())
      {
        return std::nullopt;
      }

      const std::string name =
          trim_copy((*root)["name"].get<std::string>());

      if (name.empty())
        return std::nullopt;

      return name;
    }

    std::set<std::string> read_required_production_keys()
    {
      std::set<std::string> keys;

      const fs::path path = fs::current_path() / "vix.json";
      const auto root = read_json_file(path);

      if (!root ||
          !root->is_object() ||
          !root->contains("production") ||
          !(*root)["production"].is_object())
      {
        return keys;
      }

      const auto &production = (*root)["production"];

      if (!production.contains("env") ||
          !production["env"].is_object())
      {
        return keys;
      }

      const auto &env = production["env"];

      if (!env.contains("required") ||
          !env["required"].is_array())
      {
        return keys;
      }

      for (const auto &item : env["required"])
      {
        if (!item.is_string())
          continue;

        const std::string key = trim_copy(item.get<std::string>());

        if (!key.empty())
          keys.insert(key);
      }

      return keys;
    }

    std::optional<std::string> read_service_name()
    {
      const fs::path path = fs::current_path() / "vix.json";
      const auto root = read_json_file(path);

      if (!root ||
          !root->is_object() ||
          !root->contains("production") ||
          !(*root)["production"].is_object())
      {
        return std::nullopt;
      }

      const auto &production = (*root)["production"];

      if (production.contains("service") &&
          production["service"].is_object() &&
          production["service"].contains("name") &&
          production["service"]["name"].is_string())
      {
        const std::string name =
            trim_copy(production["service"]["name"].get<std::string>());

        if (!name.empty())
          return name;
      }

      if (production.contains("deploy") &&
          production["deploy"].is_object() &&
          production["deploy"].contains("service") &&
          production["deploy"]["service"].is_string())
      {
        const std::string name =
            trim_copy(production["deploy"]["service"].get<std::string>());

        if (!name.empty())
          return name;
      }

      return std::nullopt;
    }

    std::string shell_quote(const std::string &value)
    {
#ifdef _WIN32
      return "\"" + value + "\"";
#else
      std::string out = "'";

      for (char c : value)
      {
        if (c == '\'')
          out += "'\\''";
        else
          out += c;
      }

      out += "'";
      return out;
#endif
    }

    std::optional<std::string> run_capture(const std::string &cmd)
    {
#ifdef _WIN32
      FILE *pipe = _popen(cmd.c_str(), "r");
#else
      FILE *pipe = popen(cmd.c_str(), "r");
#endif

      if (!pipe)
        return std::nullopt;

      std::string out;
      char buffer[2048];

      while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
        out += buffer;

#ifdef _WIN32
      _pclose(pipe);
#else
      pclose(pipe);
#endif

      out = trim_copy(out);

      if (out.empty())
        return std::nullopt;

      return out;
    }

    std::map<std::string, std::string> read_systemd_environment(
        const std::string &serviceName)
    {
      if (serviceName.empty())
        return {};

#ifdef _WIN32
      (void)serviceName;
      return {};
#else
      const std::string cmd =
          "systemctl show " +
          shell_quote(serviceName) +
          " -p Environment --value 2>/dev/null";

      const auto raw = run_capture(cmd);

      if (!raw)
        return {};

      return parse_systemd_environment(*raw);
#endif
    }

    void strip_inline_comment(std::string &value)
    {
      bool in_single = false;
      bool in_double = false;

      for (std::size_t i = 0; i < value.size(); ++i)
      {
        const char c = value[i];

        if (c == '\'' && !in_double)
        {
          in_single = !in_single;
          continue;
        }

        if (c == '"' && !in_single)
        {
          in_double = !in_double;
          continue;
        }

        if (c == '#' && !in_single && !in_double)
        {
          if (i == 0 ||
              std::isspace(static_cast<unsigned char>(value[i - 1])))
          {
            value = value.substr(0, i);
            return;
          }
        }
      }
    }
  }

  bool is_secret_key(const std::string &key)
  {
    const std::string lower = lower_copy(key);

    return lower.find("secret") != std::string::npos ||
           lower.find("password") != std::string::npos ||
           lower.find("passwd") != std::string::npos ||
           lower.find("token") != std::string::npos ||
           lower.find("api_key") != std::string::npos ||
           lower.find("apikey") != std::string::npos ||
           lower.find("private_key") != std::string::npos ||
           lower.find("credential") != std::string::npos;
  }

  EnvFileData read_env_file(const std::filesystem::path &path)
  {
    EnvFileData data;
    data.path = path;

    std::error_code ec;
    data.exists = fs::exists(path, ec) && !ec && fs::is_regular_file(path, ec);

    if (!data.exists)
      return data;

    std::ifstream in(path);

    if (!in)
      return data;

    std::string line;

    while (std::getline(in, line))
    {
      line = trim_copy(line);

      if (line.empty() || starts_with(line, "#"))
        continue;

      if (starts_with(line, "export "))
        line = trim_copy(line.substr(7));

      const auto eq = line.find('=');

      if (eq == std::string::npos)
        continue;

      std::string key = trim_copy(line.substr(0, eq));
      std::string value = line.substr(eq + 1);

      strip_inline_comment(value);
      value = unquote_value(value);

      if (key.empty())
        continue;

      data.values[key] = value;
    }

    return data;
  }

  std::map<std::string, std::string> parse_systemd_environment(
      const std::string &raw)
  {
    std::map<std::string, std::string> values;

    std::istringstream stream(raw);
    std::string token;

    while (stream >> token)
    {
      const auto eq = token.find('=');

      if (eq == std::string::npos)
        continue;

      const std::string key = trim_copy(token.substr(0, eq));
      const std::string value = unquote_value(token.substr(eq + 1));

      if (key.empty())
        continue;

      values[key] = value;
    }

    return values;
  }

  EnvConfig load_env_config(const EnvOptions &options)
  {
    EnvConfig cfg;

    cfg.projectDir = fs::current_path();
    cfg.appName = read_project_name().value_or(
        cfg.projectDir.filename().string().empty()
            ? std::string("vix-app")
            : cfg.projectDir.filename().string());

    cfg.env = read_env_file(cfg.projectDir / ".env");
    cfg.example = read_env_file(cfg.projectDir / ".env.example");

    if (options.production)
    {
      cfg.requiredProductionKeys = read_required_production_keys();
      cfg.serviceName = read_service_name().value_or("");

      if (!cfg.serviceName.empty())
        cfg.systemdEnvironment = read_systemd_environment(cfg.serviceName);
    }

    return cfg;
  }
}
