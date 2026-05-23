/**
 *
 *  @file NginxGenerator.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_NGINX_GENERATOR_HPP
#define VIX_NGINX_GENERATOR_HPP

#include <vix/cli/commands/proxy/ProxyTypes.hpp>

#include <string>

namespace vix::commands::proxy::nginx_generator
{
  /**
   * @brief Render the Nginx reverse proxy configuration.
   *
   * @param cfg Loaded Nginx proxy configuration.
   * @return Generated Nginx configuration.
   */
  std::string render_config(const NginxProxyConfig &cfg);

  /**
   * @brief Generate, install, enable and reload an Nginx proxy config.
   *
   * @param cfg Loaded Nginx proxy configuration.
   * @return Process exit code.
   */
  int init(const NginxProxyConfig &cfg);
}

#endif
