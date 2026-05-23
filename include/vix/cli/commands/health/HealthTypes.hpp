/**
 *
 *  @file HealthTypes.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_HEALTH_TYPES_HPP
#define VIX_HEALTH_TYPES_HPP

#include <cstdint>
#include <optional>
#include <string>

namespace vix::commands::health
{
  /**
   * @enum HealthTarget
   * @brief Health check target type.
   */
  enum class HealthTarget
  {
    Local,
    Public,
    WebSocket
  };

  /**
   * @struct HealthEndpointConfig
   * @brief Configuration for one health endpoint.
   */
  struct HealthEndpointConfig
  {
    /**
     * @brief Whether this endpoint is configured and can be checked.
     */
    bool enabled{false};

    /**
     * @brief Endpoint URL.
     */
    std::string url{};

    /**
     * @brief Expected HTTP status code.
     */
    int expectedStatus{200};

    /**
     * @brief Request timeout in milliseconds.
     */
    std::uint64_t timeoutMs{5000};

    /**
     * @brief Maximum accepted response time in milliseconds.
     */
    std::uint64_t maxResponseMs{1000};
  };

  /**
   * @struct HealthConfig
   * @brief Production health check configuration.
   */
  struct HealthConfig
  {
    /**
     * @brief Application name.
     */
    std::string appName{"vix-app"};

    /**
     * @brief Optional systemd service name checked before health requests.
     */
    std::optional<std::string> serviceName{};

    /**
     * @brief Local internal application endpoint.
     */
    HealthEndpointConfig local{};

    /**
     * @brief Public HTTPS endpoint.
     */
    HealthEndpointConfig publicEndpoint{};

    /**
     * @brief Public WebSocket endpoint.
     */
    HealthEndpointConfig websocket{};
  };

  /**
   * @struct HealthResult
   * @brief Result of one health check.
   */
  struct HealthResult
  {
    /**
     * @brief Checked target.
     */
    HealthTarget target{HealthTarget::Local};

    /**
     * @brief Checked URL.
     */
    std::string url{};

    /**
     * @brief Expected HTTP status code.
     */
    int expectedStatus{200};

    /**
     * @brief Actual HTTP status code.
     */
    int actualStatus{0};

    /**
     * @brief Measured response time in milliseconds.
     */
    std::uint64_t responseMs{0};

    /**
     * @brief Maximum accepted response time in milliseconds.
     */
    std::uint64_t maxResponseMs{1000};

    /**
     * @brief Whether the endpoint is healthy.
     */
    bool healthy{false};

    /**
     * @brief Human-readable error message when unhealthy.
     */
    std::string error{};
  };
}

#endif
