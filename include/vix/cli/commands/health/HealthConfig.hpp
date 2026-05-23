/**
 *
 *  @file HealthConfig.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_HEALTH_CONFIG_HPP
#define VIX_HEALTH_CONFIG_HPP

#include <vix/cli/commands/health/HealthTypes.hpp>

#include <optional>
#include <string>

namespace vix::commands::health
{
  /**
   * @brief Read the current Vix project name.
   *
   * @return The detected project name, or std::nullopt if no name can be resolved.
   */
  std::optional<std::string> read_project_name();

  /**
   * @brief Load production health configuration from vix.json.
   *
   * The configuration is read from `production.health`.
   *
   * @return Loaded health check configuration.
   */
  HealthConfig load_health_config();

  /**
   * @brief Return a readable name for a health target.
   *
   * @param target Health target.
   * @return Target name.
   */
  const char *target_name(HealthTarget target) noexcept;
}

#endif
