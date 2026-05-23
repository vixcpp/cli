/**
 *
 *  @file LogsConfig.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_LOGS_CONFIG_HPP
#define VIX_LOGS_CONFIG_HPP

#include <vix/cli/commands/logs/LogsTypes.hpp>

#include <optional>
#include <string>

namespace vix::commands::logs
{
  /**
   * @brief Read the current Vix project name.
   *
   * The project name is resolved from `vix.json` when available.
   * If no project name is found, the implementation may fall back to
   * the current directory name.
   *
   * @return The detected project name, or std::nullopt if no name can be resolved.
   */
  std::optional<std::string> read_project_name();

  /**
   * @brief Load production logs configuration from vix.json.
   *
   * The configuration is read from `production.logs`.
   *
   * Fallbacks:
   * - service name from `production.logs.service`
   * - then `production.deploy.service`
   * - then `production.service.name`
   * - then project name
   *
   * @return Loaded logs configuration.
   */
  LogsConfig load_logs_config();

  /**
   * @brief Apply command-line logs options to the loaded config.
   *
   * @param cfg Loaded logs configuration.
   * @param options Runtime logs options.
   * @return Effective logs configuration.
   */
  LogsConfig apply_logs_options(
      LogsConfig cfg,
      const LogsOptions &options);
}

#endif
