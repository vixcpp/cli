/**
 *
 *  @file EnvChecker.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/env/EnvChecker.hpp>
#include <vix/cli/commands/env/EnvConfig.hpp>
#include <vix/cli/commands/env/EnvOutput.hpp>

#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace vix::commands::env::checker
{
  namespace
  {
    std::set<std::string> collect_keys(const EnvConfig &cfg)
    {
      std::set<std::string> keys;

      for (const auto &[key, _] : cfg.env.values)
        keys.insert(key);

      for (const auto &[key, _] : cfg.example.values)
        keys.insert(key);

      for (const auto &key : cfg.requiredProductionKeys)
        keys.insert(key);

      for (const auto &[key, _] : cfg.systemdEnvironment)
        keys.insert(key);

      return keys;
    }

    bool value_differs(
        const std::string &left,
        const std::string &right)
    {
      return left != right;
    }

    std::string env_value_or_empty(
        const std::map<std::string, std::string> &values,
        const std::string &key)
    {
      const auto it = values.find(key);

      if (it == values.end())
        return {};

      return it->second;
    }

    bool has_key(
        const std::map<std::string, std::string> &values,
        const std::string &key)
    {
      return values.find(key) != values.end();
    }

    bool has_key(
        const std::set<std::string> &values,
        const std::string &key)
    {
      return values.find(key) != values.end();
    }
  }

  EnvCheckResult analyze(
      const EnvConfig &cfg,
      const EnvOptions &options)
  {
    EnvCheckResult result;

    const std::set<std::string> keys = collect_keys(cfg);

    for (const auto &key : keys)
    {
      EnvVariable variable;

      variable.name = key;
      variable.presentInEnv = has_key(cfg.env.values, key);
      variable.presentInExample = has_key(cfg.example.values, key);
      variable.presentInSystemd = has_key(cfg.systemdEnvironment, key);
      variable.required = has_key(cfg.requiredProductionKeys, key);
      variable.secret = is_secret_key(key);

      if (variable.presentInEnv)
        variable.value = env_value_or_empty(cfg.env.values, key);
      else if (variable.presentInSystemd)
        variable.value = env_value_or_empty(cfg.systemdEnvironment, key);
      else if (variable.presentInExample)
        variable.value = env_value_or_empty(cfg.example.values, key);

      result.variables.push_back(variable);

      if (cfg.example.exists &&
          variable.presentInExample &&
          !variable.presentInEnv)
      {
        result.missingFromEnv.push_back(key);
      }

      if (cfg.env.exists &&
          variable.presentInEnv &&
          !variable.presentInExample)
      {
        result.missingFromExample.push_back(key);
      }

      if (options.production &&
          variable.required &&
          !variable.presentInEnv &&
          !variable.presentInSystemd)
      {
        result.missingRequiredProduction.push_back(key);
      }

      if (options.production &&
          variable.presentInEnv &&
          variable.presentInSystemd)
      {
        const std::string envValue =
            env_value_or_empty(cfg.env.values, key);

        const std::string systemdValue =
            env_value_or_empty(cfg.systemdEnvironment, key);

        if (value_differs(envValue, systemdValue))
          result.systemdDiffers.push_back(key);
      }
    }

    std::sort(
        result.variables.begin(),
        result.variables.end(),
        [](const EnvVariable &a, const EnvVariable &b)
        {
          return a.name < b.name;
        });

    result.ok =
        result.missingFromEnv.empty() &&
        result.missingRequiredProduction.empty() &&
        result.systemdDiffers.empty();

    return result;
  }

  int check(
      const EnvConfig &cfg,
      const EnvOptions &options)
  {
    output::print_summary(std::cout, cfg, options);

    const EnvCheckResult result = analyze(cfg, options);

    output::section(std::cout, "Variables");

    if (result.variables.empty())
    {
      output::warn(std::cerr, "no environment variables found");
      output::fix(std::cerr, "create .env and .env.example");
      return 1;
    }

    for (const auto &variable : result.variables)
      output::print_variable(std::cout, variable, options);

    if (!result.missingFromEnv.empty())
    {
      output::section(std::cerr, "Missing from .env");

      for (const auto &key : result.missingFromEnv)
        output::error(std::cerr, key);

      output::fix(std::cerr, "copy required keys from .env.example to .env");
    }

    if (!result.missingFromExample.empty())
    {
      output::section(std::cerr, "Missing from .env.example");

      for (const auto &key : result.missingFromExample)
        output::warn(std::cerr, key);

      output::fix(std::cerr, "document project env keys in .env.example");
    }

    if (!result.missingRequiredProduction.empty())
    {
      output::section(std::cerr, "Missing required production env");

      for (const auto &key : result.missingRequiredProduction)
        output::error(std::cerr, key);

      output::fix(
          std::cerr,
          "set missing keys in .env or systemd Environment");
    }

    if (!result.systemdDiffers.empty())
    {
      output::section(std::cerr, "Systemd env differs");

      for (const auto &key : result.systemdDiffers)
        output::warn(std::cerr, key);

      output::fix(
          std::cerr,
          "update the systemd service environment, then run `sudo systemctl daemon-reload` and restart the service");
    }

    if (!cfg.env.exists)
    {
      output::warn(std::cerr, ".env not found");
      output::fix(std::cerr, "create .env from .env.example");
    }

    if (!cfg.example.exists)
    {
      output::warn(std::cerr, ".env.example not found");
      output::fix(std::cerr, "create .env.example to document required variables");
    }

    if (result.ok)
    {
      output::ok(std::cout, "environment check passed");
      return 0;
    }

    output::error(std::cerr, "environment check failed");
    return 1;
  }
}
