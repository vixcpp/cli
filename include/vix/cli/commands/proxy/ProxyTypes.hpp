/**
 *
 *  @file ProxyTypes.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_PROXY_TYPES_HPP
#define VIX_PROXY_TYPES_HPP

#include <filesystem>
#include <string>

namespace vix::commands::proxy
{
  /**
   * @brief Configuration used to generate and validate an Nginx reverse proxy.
   */
  struct NginxProxyConfig
  {
    /**
     * @brief Application name used for display and generated file names.
     */
    std::string appName{"vix-app"};

    /**
     * @brief Public domain handled by Nginx.
     */
    std::string domain{};

    /**
     * @brief Local HTTP upstream port used by the Vix application.
     */
    int httpPort{8080};

    /**
     * @brief Whether the application exposes a WebSocket upstream.
     */
    bool websocketEnabled{false};

    /**
     * @brief Public WebSocket path handled by Nginx.
     */
    std::string websocketPath{"/ws"};

    /**
     * @brief Local WebSocket upstream port.
     */
    int websocketPort{9090};

    /**
     * @brief Whether TLS is enabled for the generated Nginx config.
     */
    bool tlsEnabled{true};

    /**
     * @brief Path to the TLS certificate file.
     */
    std::filesystem::path certificatePath{};

    /**
     * @brief Path to the TLS private key file.
     */
    std::filesystem::path certificateKeyPath{};

    /**
     * @brief Destination path under /etc/nginx/sites-available.
     */
    std::filesystem::path sitesAvailablePath{};

    /**
     * @brief Destination path under /etc/nginx/sites-enabled.
     */
    std::filesystem::path sitesEnabledPath{};
  };
}

#endif
