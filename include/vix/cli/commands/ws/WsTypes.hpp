/**
 *
 *  @file WsTypes.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_WS_TYPES_HPP
#define VIX_WS_TYPES_HPP

#include <cstdint>
#include <string>

namespace vix::commands::ws
{
  /**
   * @enum WsTarget
   * @brief Target selected by `vix ws`.
   */
  enum class WsTarget
  {
    Check
  };

  /**
   * @struct WsOptions
   * @brief Runtime options passed from the ws command line.
   */
  struct WsOptions
  {
    /**
     * @brief Selected WebSocket command target.
     */
    WsTarget target{WsTarget::Check};

    /**
     * @brief WebSocket URL to check.
     *
     * Example:
     * ws://127.0.0.1:9090/ws
     */
    std::string url{};

    /**
     * @brief Connection timeout in milliseconds.
     */
    std::uint64_t timeoutMs{3000};

    /**
     * @brief Whether to send a ping after connection.
     */
    bool ping{true};

    /**
     * @brief Whether to print additional diagnostics.
     */
    bool verbose{false};
  };

  /**
   * @struct WsConfig
   * @brief Production WebSocket configuration.
   */
  struct WsConfig
  {
    /**
     * @brief Application name.
     */
    std::string appName{"vix-app"};

    /**
     * @brief Public WebSocket URL.
     */
    std::string publicUrl{};

    /**
     * @brief Local WebSocket URL.
     */
    std::string localUrl{};

    /**
     * @brief WebSocket host.
     */
    std::string host{"127.0.0.1"};

    /**
     * @brief WebSocket port.
     */
    int port{9090};

    /**
     * @brief WebSocket route/path.
     */
    std::string path{"/ws"};

    /**
     * @brief Default timeout in milliseconds.
     */
    std::uint64_t timeoutMs{3000};

    /**
     * @brief Whether heartbeat diagnostics should be enabled.
     */
    bool heartbeat{true};
  };
}

#endif
