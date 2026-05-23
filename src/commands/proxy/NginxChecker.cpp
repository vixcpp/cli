/**
 *
 *  @file NginxChecker.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/proxy/NginxChecker.hpp>
#include <vix/cli/commands/proxy/NginxOutput.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace vix::commands::proxy::nginx_checker
{
  namespace
  {
    std::string shell_quote(const std::string &value)
    {
      std::string out = "'";

      for (char c : value)
      {
        if (c == '\'')
          out += "'\\''";
        else
          out += c;
      }

      out += "'";
      return out;
    }

    bool run_cmd(const std::string &cmd)
    {
      return std::system(cmd.c_str()) == 0;
    }

    bool file_contains(
        const fs::path &path,
        const std::string &needle)
    {
      std::ifstream in(path);

      if (!in)
        return false;

      std::ostringstream ss;
      ss << in.rdbuf();

      return ss.str().find(needle) != std::string::npos;
    }

    std::string http_upstream(const NginxProxyConfig &cfg)
    {
      return "proxy_pass http://127.0.0.1:" + std::to_string(cfg.httpPort);
    }

    std::string websocket_upstream(const NginxProxyConfig &cfg)
    {
      return "proxy_pass http://127.0.0.1:" +
             std::to_string(cfg.websocketPort) +
             "/";
    }

    bool site_enabled(const NginxProxyConfig &cfg)
    {
      std::error_code ec;

      if (!fs::exists(cfg.sitesEnabledPath, ec))
        return false;

      if (!fs::is_symlink(cfg.sitesEnabledPath, ec))
        return false;

      const fs::path target = fs::read_symlink(cfg.sitesEnabledPath, ec);

      if (ec)
        return false;

      return target == cfg.sitesAvailablePath;
    }

    bool check_required_content(const NginxProxyConfig &cfg)
    {
      bool ok = true;

      if (!file_contains(cfg.sitesAvailablePath, "server_name " + cfg.domain))
      {
        nginx_output::error(std::cerr, "Missing or wrong server_name.");
        nginx_output::fix(std::cerr, "run `vix proxy nginx init`");
        ok = false;
      }

      if (!file_contains(cfg.sitesAvailablePath, http_upstream(cfg)))
      {
        nginx_output::error(std::cerr, "Missing or wrong HTTP upstream port.");
        nginx_output::fix(std::cerr, "check production.proxy.http.port in vix.json, then run `vix proxy nginx init`");
        ok = false;
      }

      if (!file_contains(cfg.sitesAvailablePath, "proxy_set_header Host $host") ||
          !file_contains(cfg.sitesAvailablePath, "proxy_set_header X-Real-IP $remote_addr") ||
          !file_contains(cfg.sitesAvailablePath, "proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for") ||
          !file_contains(cfg.sitesAvailablePath, "proxy_set_header X-Forwarded-Proto $scheme"))
      {
        nginx_output::error(std::cerr, "Missing X-Forwarded proxy headers.");
        nginx_output::fix(std::cerr, "run `vix proxy nginx init`");
        ok = false;
      }

      if (!file_contains(cfg.sitesAvailablePath, "proxy_connect_timeout 10s") ||
          !file_contains(cfg.sitesAvailablePath, "proxy_send_timeout 60s") ||
          !file_contains(cfg.sitesAvailablePath, "proxy_read_timeout 60s"))
      {
        nginx_output::error(std::cerr, "Missing HTTP proxy timeouts.");
        nginx_output::fix(std::cerr, "run `vix proxy nginx init`");
        ok = false;
      }

      if (cfg.websocketEnabled)
      {
        if (!file_contains(cfg.sitesAvailablePath, "location = " + cfg.websocketPath))
        {
          nginx_output::error(std::cerr, "Missing WebSocket location.");
          nginx_output::fix(std::cerr, "check production.proxy.websocket.path in vix.json, then run `vix proxy nginx init`");
          ok = false;
        }

        if (!file_contains(cfg.sitesAvailablePath, websocket_upstream(cfg)))
        {
          nginx_output::error(std::cerr, "Missing or wrong WebSocket upstream port.");
          nginx_output::fix(std::cerr, "check production.proxy.websocket.port in vix.json, then run `vix proxy nginx init`");
          ok = false;
        }

        if (!file_contains(cfg.sitesAvailablePath, "proxy_set_header Upgrade $http_upgrade") ||
            !file_contains(cfg.sitesAvailablePath, "proxy_set_header Connection \"upgrade\""))
        {
          nginx_output::error(std::cerr, "Missing WebSocket upgrade headers.");
          nginx_output::fix(std::cerr, "run `vix proxy nginx init`");
          ok = false;
        }

        if (!file_contains(cfg.sitesAvailablePath, "proxy_read_timeout 3600s") ||
            !file_contains(cfg.sitesAvailablePath, "proxy_send_timeout 3600s") ||
            !file_contains(cfg.sitesAvailablePath, "proxy_buffering off"))
        {
          nginx_output::error(std::cerr, "Missing WebSocket proxy timeouts.");
          nginx_output::fix(std::cerr, "run `vix proxy nginx init`");
          ok = false;
        }
      }

      if (cfg.tlsEnabled)
      {
        if (!file_contains(cfg.sitesAvailablePath, "ssl_certificate " + cfg.certificatePath.string()) ||
            !file_contains(cfg.sitesAvailablePath, "ssl_certificate_key " + cfg.certificateKeyPath.string()))
        {
          nginx_output::error(std::cerr, "Missing or wrong TLS certificate paths.");
          nginx_output::fix(std::cerr, "check production.proxy.tls in vix.json, then run `vix proxy nginx init`");
          ok = false;
        }

        if (!file_contains(cfg.sitesAvailablePath, "return 301 https://$host$request_uri"))
        {
          nginx_output::error(std::cerr, "Missing HTTPS redirect.");
          nginx_output::fix(std::cerr, "run `vix proxy nginx init`");
          ok = false;
        }
      }

      return ok;
    }
  }

  int check(const NginxProxyConfig &cfg)
  {
    const bool enabled = site_enabled(cfg);

    nginx_output::print_check_summary(std::cout, cfg, enabled);

    bool ok = true;

    if (cfg.domain.empty())
    {
      nginx_output::error(std::cerr, "Missing proxy domain.");
      nginx_output::fix(std::cerr, "add production.proxy.domain to vix.json");
      ok = false;
    }

    if (!fs::exists(cfg.sitesAvailablePath))
    {
      nginx_output::error(
          std::cerr,
          "Nginx site file not found: " + cfg.sitesAvailablePath.string());

      nginx_output::fix(std::cerr, "run `vix proxy nginx init`");
      return 1;
    }

    if (!enabled)
    {
      nginx_output::error(std::cerr, "Nginx site is not enabled.");
      nginx_output::fix(
          std::cerr,
          "sudo ln -sfn " +
              cfg.sitesAvailablePath.string() +
              " " +
              cfg.sitesEnabledPath.string());

      ok = false;
    }

    if (!check_required_content(cfg))
      ok = false;

    if (!run_cmd("command -v nginx >/dev/null 2>&1"))
    {
      nginx_output::error(std::cerr, "Nginx is not installed or not available in PATH.");
      nginx_output::fix(std::cerr, "install nginx, then run `vix proxy nginx init`");
      ok = false;
    }
    else if (!run_cmd("sudo nginx -t"))
    {
      nginx_output::error(std::cerr, "Nginx config is invalid.");
      nginx_output::fix(std::cerr, "sudo nginx -t");
      ok = false;
    }
    else
    {
      nginx_output::ok(std::cout, "nginx config is valid");
    }

    if (!ok)
      return 1;

    nginx_output::ok(std::cout, "proxy config looks good");
    return 0;
  }

  int reload(const NginxProxyConfig &cfg)
  {
    nginx_output::print_reload_summary(std::cout, cfg);

    if (!run_cmd("command -v nginx >/dev/null 2>&1"))
    {
      nginx_output::error(std::cerr, "Nginx is not installed or not available in PATH.");
      nginx_output::fix(std::cerr, "install nginx");
      return 1;
    }

    if (!run_cmd("sudo nginx -t"))
    {
      nginx_output::error(std::cerr, "Nginx config is invalid.");
      nginx_output::fix(std::cerr, "sudo nginx -t");
      return 1;
    }

    nginx_output::ok(std::cout, "nginx config is valid");

    if (!run_cmd("sudo systemctl reload nginx"))
    {
      nginx_output::error(std::cerr, "Failed to reload Nginx.");
      nginx_output::fix(std::cerr, "sudo systemctl reload nginx");
      return 1;
    }

    nginx_output::ok(std::cout, "nginx reloaded");
    return 0;
  }
}
