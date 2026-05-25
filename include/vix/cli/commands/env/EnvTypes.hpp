/**
 *
 *  @file EnvTypes.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_ENV_TYPES_HPP
#define VIX_CLI_ENV_TYPES_HPP

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace vix::commands::env
{
  /**
   * @brief Runtime options for `vix env`.
   */
  struct EnvOptions
  {
    /**
     * @brief Validate production environment requirements.
     */
    bool production{false};

    /**
     * @brief Mask values when printing them.
     */
    bool masked{true};

    /**
     * @brief Print values explicitly. Secrets stay masked unless unsafe output
     * is enabled later.
     */
    bool showValues{false};
  };

  /**
   * @brief Parsed key/value environment file.
   */
  struct EnvFileData
  {
    std::filesystem::path path{};
    bool exists{false};
    std::map<std::string, std::string> values{};
  };

  /**
   * @brief Environment configuration loaded from project files and production config.
   */
  struct EnvConfig
  {
    /**
     * @brief Application name.
     */
    std::string appName{"vix-app"};

    /**
     * @brief Project root.
     */
    std::filesystem::path projectDir{};

    /**
     * @brief `.env` file data.
     */
    EnvFileData env{};

    /**
     * @brief `.env.example` file data.
     */
    EnvFileData example{};

    /**
     * @brief Required production environment variables.
     */
    std::set<std::string> requiredProductionKeys{};

    /**
     * @brief Environment variables exposed by systemd.
     */
    std::map<std::string, std::string> systemdEnvironment{};

    /**
     * @brief systemd service name used for production environment comparison.
     */
    std::string serviceName{};
  };

  /**
   * @brief A single environment variable report item.
   */
  struct EnvVariable
  {
    std::string name{};
    std::string value{};
    bool presentInEnv{false};
    bool presentInExample{false};
    bool presentInSystemd{false};
    bool required{false};
    bool secret{false};
  };

  /**
   * @brief Result of an environment check.
   */
  struct EnvCheckResult
  {
    bool ok{true};
    std::vector<EnvVariable> variables{};
    std::vector<std::string> missingFromEnv{};
    std::vector<std::string> missingFromExample{};
    std::vector<std::string> missingRequiredProduction{};
    std::vector<std::string> systemdDiffers{};
  };
}

#endif
