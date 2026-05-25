/**
 *
 *  @file NginxOutput.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/proxy/NginxOutput.hpp>
#include <vix/cli/util/Ui.hpp>

#include <iostream>
#include <ostream>
#include <string>

namespace vix::commands::proxy::nginx_output
{
  namespace
  {
    std::string yes_no(bool value)
    {
      return value ? "yes" : "no";
    }

    std::string enabled_disabled(bool value)
    {
      return value ? "enabled" : "disabled";
    }

    std::string websocket_value(const NginxProxyConfig &cfg)
    {
      if (!cfg.websocketEnabled)
        return "disabled";

      return "127.0.0.1:" +
             std::to_string(cfg.websocketPort) +
             " " +
             cfg.websocketPath;
    }

    std::string tls_value(const NginxProxyConfig &cfg)
    {
      return cfg.tlsEnabled ? "enabled" : "disabled";
    }
  }

  void print_init_summary(
      std::ostream &out,
      const NginxProxyConfig &cfg)
  {
    vix::cli::util::section(out, "Proxy Init");

    vix::cli::util::kv(out, "App", cfg.appName);
    vix::cli::util::kv(out, "Domain", cfg.domain.empty() ? "(missing)" : cfg.domain);
    vix::cli::util::kv(out, "HTTP", "127.0.0.1:" + std::to_string(cfg.httpPort));
    vix::cli::util::kv(out, "WebSocket", websocket_value(cfg));
    vix::cli::util::kv(out, "TLS", tls_value(cfg));
    vix::cli::util::kv(out, "Site file", cfg.sitesAvailablePath.string());
    vix::cli::util::kv(out, "Enabled path", cfg.sitesEnabledPath.string());

    if (cfg.tlsEnabled)
    {
      vix::cli::util::kv(out, "Certificate", cfg.certificatePath.string());
      vix::cli::util::kv(out, "Certificate key", cfg.certificateKeyPath.string());
    }
  }

  void print_check_summary(
      std::ostream &out,
      const NginxProxyConfig &cfg,
      bool enabled)
  {
    vix::cli::util::section(out, "Proxy Check");

    vix::cli::util::kv(out, "App", cfg.appName);
    vix::cli::util::kv(out, "Domain", cfg.domain.empty() ? "(missing)" : cfg.domain);
    vix::cli::util::kv(out, "HTTP upstream", "127.0.0.1:" + std::to_string(cfg.httpPort));
    vix::cli::util::kv(out, "WS upstream", websocket_value(cfg));
    vix::cli::util::kv(out, "TLS", enabled_disabled(cfg.tlsEnabled));
    vix::cli::util::kv(out, "Site file", cfg.sitesAvailablePath.string());
    vix::cli::util::kv(out, "Enabled", yes_no(enabled));
  }

  void print_reload_summary(
      std::ostream &out,
      const NginxProxyConfig &cfg)
  {
    vix::cli::util::section(out, "Proxy Reload");

    vix::cli::util::kv(out, "App", cfg.appName);
    vix::cli::util::kv(out, "Domain", cfg.domain.empty() ? "(missing)" : cfg.domain);
    vix::cli::util::kv(out, "Site file", cfg.sitesAvailablePath.string());
  }

  void ok(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::ok_line(out, message);
  }

  void warn(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::warn_line(out, message);
  }

  void error(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::err_line(out, message);
  }

  void fix(
      std::ostream &out,
      const std::string &command)
  {
    vix::cli::util::warn_line(out, "Fix: " + command);
  }
}
