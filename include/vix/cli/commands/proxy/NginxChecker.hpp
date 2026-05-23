/**
 *
 *  @file NginxChecker.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_NGINX_CHECKER_HPP
#define VIX_NGINX_CHECKER_HPP

#include <vix/cli/commands/proxy/ProxyTypes.hpp>

namespace vix::commands::proxy::nginx_checker
{
  /**
   * @brief Validate the generated Nginx reverse proxy configuration.
   *
   * This checks the installed site file, enabled symlink, proxy upstreams,
   * required headers, proxy timeouts and `nginx -t`.
   *
   * @param cfg Loaded Nginx proxy configuration.
   * @return Process exit code.
   */
  int check(const NginxProxyConfig &cfg);

  /**
   * @brief Validate and reload Nginx.
   *
   * This runs `nginx -t` before reloading the Nginx service.
   *
   * @param cfg Loaded Nginx proxy configuration.
   * @return Process exit code.
   */
  int reload(const NginxProxyConfig &cfg);
}

#endif
