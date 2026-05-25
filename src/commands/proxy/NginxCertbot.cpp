/**
 *
 *  @file NginxCertbot.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/proxy/NginxCertbot.hpp>
#include <vix/cli/commands/proxy/NginxGenerator.hpp>
#include <vix/cli/commands/proxy/NginxOutput.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace vix::commands::proxy::nginx_certbot
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

    fs::path default_certificate_path(const std::string &domain)
    {
      return fs::path("/etc/letsencrypt/live") / domain / "fullchain.pem";
    }

    fs::path default_certificate_key_path(const std::string &domain)
    {
      return fs::path("/etc/letsencrypt/live") / domain / "privkey.pem";
    }

    bool install_rendered_config(const NginxProxyConfig &cfg)
    {
      const std::string config = nginx_generator::render_config(cfg);
      const fs::path tmp = fs::temp_directory_path() / (cfg.appName + ".nginx");

      {
        std::ofstream out(tmp);

        if (!out)
        {
          nginx_output::error(
              std::cerr,
              "Failed to write temporary Nginx config: " + tmp.string());

          return false;
        }

        out << config;
      }

      if (!run_cmd(
              "sudo cp " +
              shell_quote(tmp.string()) +
              " " +
              shell_quote(cfg.sitesAvailablePath.string())))
      {
        nginx_output::error(std::cerr, "Failed to install Nginx site config.");
        run_cmd("rm -f " + shell_quote(tmp.string()));
        return false;
      }

      run_cmd("rm -f " + shell_quote(tmp.string()));

      if (!run_cmd(
              "sudo ln -sfn " +
              shell_quote(cfg.sitesAvailablePath.string()) +
              " " +
              shell_quote(cfg.sitesEnabledPath.string())))
      {
        nginx_output::error(std::cerr, "Failed to enable Nginx site.");
        return false;
      }

      return true;
    }

    bool nginx_test()
    {
      if (!run_cmd("sudo nginx -t"))
      {
        nginx_output::error(std::cerr, "Nginx config is invalid.");
        nginx_output::fix(std::cerr, "sudo nginx -t");
        return false;
      }

      nginx_output::ok(std::cout, "nginx config is valid");
      return true;
    }

    bool reload_or_start_nginx()
    {
      if (run_cmd("systemctl is-active --quiet nginx"))
      {
        if (!run_cmd("sudo systemctl reload nginx"))
        {
          nginx_output::error(std::cerr, "Failed to reload Nginx.");
          nginx_output::fix(std::cerr, "sudo systemctl reload nginx");
          return false;
        }

        nginx_output::ok(std::cout, "nginx reloaded");
        return true;
      }

      if (!run_cmd("sudo systemctl start nginx"))
      {
        nginx_output::error(std::cerr, "Failed to start Nginx.");
        nginx_output::fix(std::cerr, "sudo systemctl start nginx");
        return false;
      }

      nginx_output::ok(std::cout, "nginx started");
      return true;
    }

    NginxProxyConfig make_http_bootstrap_config(const NginxProxyConfig &cfg)
    {
      NginxProxyConfig http_cfg = cfg;
      http_cfg.tlsEnabled = false;
      http_cfg.certificatePath.clear();
      http_cfg.certificateKeyPath.clear();
      return http_cfg;
    }

    NginxProxyConfig make_final_tls_config(const NginxProxyConfig &cfg)
    {
      NginxProxyConfig tls_cfg = cfg;
      tls_cfg.tlsEnabled = true;

      if (tls_cfg.certificatePath.empty())
        tls_cfg.certificatePath = default_certificate_path(tls_cfg.domain);

      if (tls_cfg.certificateKeyPath.empty())
        tls_cfg.certificateKeyPath = default_certificate_key_path(tls_cfg.domain);

      return tls_cfg;
    }
  }

  int run(const NginxProxyConfig &cfg)
  {
    nginx_output::print_init_summary(std::cout, cfg);

    if (cfg.domain.empty())
    {
      nginx_output::error(std::cerr, "Missing proxy domain.");
      nginx_output::fix(std::cerr, "add production.proxy.domain to vix.json");
      return 1;
    }

    if (!run_cmd("command -v nginx >/dev/null 2>&1"))
    {
      nginx_output::error(std::cerr, "Nginx is not installed or not available in PATH.");
      nginx_output::fix(std::cerr, "install nginx");
      return 1;
    }

    if (!run_cmd("command -v certbot >/dev/null 2>&1"))
    {
      nginx_output::error(std::cerr, "Certbot is not installed or not available in PATH.");
      nginx_output::fix(std::cerr, "sudo apt install certbot python3-certbot-nginx");
      return 1;
    }

    nginx_output::ok(std::cout, "preparing temporary HTTP config for Certbot");

    const NginxProxyConfig http_cfg = make_http_bootstrap_config(cfg);

    if (!install_rendered_config(http_cfg))
      return 1;

    if (!nginx_test())
      return 1;

    if (!reload_or_start_nginx())
      return 1;

    const std::string certbot_cmd =
        "sudo certbot --nginx -d " +
        shell_quote(cfg.domain);

    nginx_output::ok(std::cout, "running certbot for " + cfg.domain);

    if (!run_cmd(certbot_cmd))
    {
      nginx_output::error(std::cerr, "Certbot failed.");
      nginx_output::fix(std::cerr, certbot_cmd);
      return 1;
    }

    nginx_output::ok(std::cout, "certificate issued or renewed");

    const NginxProxyConfig tls_cfg = make_final_tls_config(cfg);

    if (!fs::exists(tls_cfg.certificatePath))
    {
      nginx_output::error(
          std::cerr,
          "Let's Encrypt certificate was not found: " +
              tls_cfg.certificatePath.string());

      nginx_output::fix(std::cerr, "check certbot output and domain DNS records");
      return 1;
    }

    if (!fs::exists(tls_cfg.certificateKeyPath))
    {
      nginx_output::error(
          std::cerr,
          "Let's Encrypt private key was not found: " +
              tls_cfg.certificateKeyPath.string());

      nginx_output::fix(std::cerr, "check certbot output and domain DNS records");
      return 1;
    }

    nginx_output::ok(std::cout, "installing final Vix TLS proxy config");

    if (!install_rendered_config(tls_cfg))
      return 1;

    if (!nginx_test())
      return 1;

    if (!reload_or_start_nginx())
      return 1;

    nginx_output::ok(std::cout, "Let's Encrypt integration completed: " + cfg.domain);
    return 0;
  }
}
