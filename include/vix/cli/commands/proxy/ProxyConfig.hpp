/**
 *
 *  @file ProxyConfig.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_PROXY_CONFIG_HPP
#define VIX_PROXY_CONFIG_HPP

#include <vix/cli/commands/proxy/ProxyTypes.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace vix::commands::proxy
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
   * @brief Load the Nginx reverse proxy configuration for the current project.
   *
   * The configuration is loaded from the `production.proxy` section of
   * `vix.json`, with sensible defaults for HTTP, WebSocket and TLS fields.
   *
   * @return Fully resolved Nginx proxy configuration.
   */
  NginxProxyConfig load_nginx_proxy_config();

  /**
   * @brief Return the target path for the generated Nginx site file.
   *
   * @param cfg Loaded Nginx proxy configuration.
   * @return Path under `/etc/nginx/sites-available`.
   */
  std::filesystem::path nginx_sites_available_path(
      const NginxProxyConfig &cfg);

  /**
   * @brief Return the target path for the enabled Nginx site symlink.
   *
   * @param cfg Loaded Nginx proxy configuration.
   * @return Path under `/etc/nginx/sites-enabled`.
   */
  std::filesystem::path nginx_sites_enabled_path(
      const NginxProxyConfig &cfg);
}

#endif
