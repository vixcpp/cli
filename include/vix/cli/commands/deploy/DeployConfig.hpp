/**
 *
 *  @file DeployConfig.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_DEPLOY_CONFIG_HPP
#define VIX_DEPLOY_CONFIG_HPP

#include <vix/cli/commands/deploy/DeployTypes.hpp>

#include <optional>
#include <string>

namespace vix::commands::deploy
{
  /**
   * @brief Read the current Vix project name.
   *
   * @return The detected project name, or std::nullopt if no name can be resolved.
   */
  std::optional<std::string> read_project_name();

  /**
   * @brief Load production deployment configuration from vix.json.
   *
   * The configuration is read from `production.deploy`.
   *
   * @return Loaded deployment configuration.
   */
  DeployConfig load_deploy_config();

  /**
   * @brief Apply command-line deploy options to the loaded config.
   *
   * @param cfg Loaded deployment configuration.
   * @param options Runtime deploy options.
   * @return Effective deployment configuration.
   */
  DeployConfig apply_deploy_options(
      DeployConfig cfg,
      const DeployOptions &options);
}

#endif
