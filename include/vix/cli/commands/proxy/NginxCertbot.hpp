/**
 *
 *  @file NginxCertbot.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_NGINX_CERTBOT_HPP
#define VIX_NGINX_CERTBOT_HPP

#include <vix/cli/commands/proxy/ProxyTypes.hpp>

namespace vix::commands::proxy::nginx_certbot
{
  /**
   * @brief Issue or renew a Let's Encrypt certificate using Certbot.
   *
   * This prepares an HTTP Nginx config first, runs Certbot with the Nginx
   * plugin, then reinstalls the final Vix TLS proxy config.
   *
   * @param cfg Loaded Nginx proxy configuration.
   * @return Process exit code.
   */
  int run(const NginxProxyConfig &cfg);
}

#endif
