/**
 *
 *  @file EnvConfig.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_ENV_CONFIG_HPP
#define VIX_CLI_ENV_CONFIG_HPP

#include <vix/cli/commands/env/EnvTypes.hpp>

#include <filesystem>
#include <map>
#include <string>

namespace vix::commands::env
{
  /**
   * @brief Load environment configuration for the current project.
   *
   * Reads:
   * - `.env`
   * - `.env.example`
   * - `production.env.required` from `vix.json`
   * - systemd Environment values when production mode is enabled
   *
   * @param options Runtime env options.
   * @return Loaded environment configuration.
   */
  EnvConfig load_env_config(const EnvOptions &options);

  /**
   * @brief Parse a dotenv-style file.
   *
   * @param path Env file path.
   * @return Parsed env file data.
   */
  EnvFileData read_env_file(const std::filesystem::path &path);

  /**
   * @brief Parse a raw systemd Environment property.
   *
   * @param raw Raw value returned by `systemctl show -p Environment --value`.
   * @return Parsed key/value map.
   */
  std::map<std::string, std::string> parse_systemd_environment(
      const std::string &raw);

  /**
   * @brief Check whether an env key looks sensitive.
   *
   * @param key Environment variable name.
   * @return true if the key should be treated as secret.
   */
  bool is_secret_key(const std::string &key);
}

#endif
