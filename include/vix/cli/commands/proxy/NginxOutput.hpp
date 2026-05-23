/**
 *
 *  @file NginxOutput.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_NGINX_OUTPUT_HPP
#define VIX_NGINX_OUTPUT_HPP

#include <vix/cli/commands/proxy/ProxyTypes.hpp>

#include <iosfwd>
#include <string>

namespace vix::commands::proxy::nginx_output
{
  /**
   * @brief Print the Nginx proxy initialization summary.
   *
   * @param out Output stream.
   * @param cfg Loaded Nginx proxy configuration.
   */
  void print_init_summary(
      std::ostream &out,
      const NginxProxyConfig &cfg);

  /**
   * @brief Print the Nginx proxy check summary.
   *
   * @param out Output stream.
   * @param cfg Loaded Nginx proxy configuration.
   * @param enabled Whether the site symlink is active.
   */
  void print_check_summary(
      std::ostream &out,
      const NginxProxyConfig &cfg,
      bool enabled);

  /**
   * @brief Print the Nginx proxy reload summary.
   *
   * @param out Output stream.
   * @param cfg Loaded Nginx proxy configuration.
   */
  void print_reload_summary(
      std::ostream &out,
      const NginxProxyConfig &cfg);

  /**
   * @brief Print a successful proxy operation message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void ok(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a proxy warning message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void warn(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a proxy error message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void error(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a fix suggestion for a proxy problem.
   *
   * @param out Output stream.
   * @param command Command or hint to print.
   */
  void fix(
      std::ostream &out,
      const std::string &command);
}

#endif
