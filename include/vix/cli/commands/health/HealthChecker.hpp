/**
 *
 *  @file HealthChecker.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_HEALTH_CHECKER_HPP
#define VIX_HEALTH_CHECKER_HPP

#include <vix/cli/commands/health/HealthTypes.hpp>

namespace vix::commands::health::checker
{
  /**
   * @brief Run all configured health checks.
   *
   * @param cfg Loaded health configuration.
   * @return Process exit code.
   */
  int check_all(const HealthConfig &cfg);

  /**
   * @brief Run the local application health check.
   *
   * @param cfg Loaded health configuration.
   * @return Process exit code.
   */
  int check_local(const HealthConfig &cfg);

  /**
   * @brief Run the public HTTPS health check.
   *
   * @param cfg Loaded health configuration.
   * @return Process exit code.
   */
  int check_public(const HealthConfig &cfg);

  /**
   * @brief Run the WebSocket health check.
   *
   * @param cfg Loaded health configuration.
   * @return Process exit code.
   */
  int check_websocket(const HealthConfig &cfg);
}

#endif
