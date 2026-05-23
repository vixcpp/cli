/**
 *
 *  @file NginxGenerator.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/proxy/NginxGenerator.hpp>
#include <vix/cli/commands/proxy/NginxOutput.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace vix::commands::proxy::nginx_generator
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

    std::string websocket_location(const NginxProxyConfig &cfg)
    {
      std::ostringstream out;

      out << "    location = " << cfg.websocketPath << " {\n";
      out << "        proxy_pass http://127.0.0.1:" << cfg.websocketPort << "/;\n";
      out << "        proxy_http_version 1.1;\n\n";

      out << "        proxy_set_header Upgrade $http_upgrade;\n";
      out << "        proxy_set_header Connection \"upgrade\";\n\n";

      out << "        proxy_set_header Host $host;\n";
      out << "        proxy_set_header X-Real-IP $remote_addr;\n";
      out << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n";
      out << "        proxy_set_header X-Forwarded-Proto $scheme;\n\n";

      out << "        proxy_read_timeout 3600s;\n";
      out << "        proxy_send_timeout 3600s;\n";
      out << "        proxy_buffering off;\n";
      out << "    }\n";

      return out.str();
    }

    std::string render_tls_config(const NginxProxyConfig &cfg)
    {
      std::ostringstream out;

      out << "server {\n";
      out << "    listen 80;\n";
      out << "    listen [::]:80;\n";
      out << "    server_name " << cfg.domain << ";\n\n";
      out << "    return 301 https://$host$request_uri;\n";
      out << "}\n\n";

      out << "server {\n";
      out << "    listen 443 ssl http2;\n";
      out << "    listen [::]:443 ssl http2;\n";
      out << "    server_name " << cfg.domain << ";\n\n";

      out << "    ssl_certificate " << cfg.certificatePath.string() << ";\n";
      out << "    ssl_certificate_key " << cfg.certificateKeyPath.string() << ";\n\n";

      out << "    location / {\n";
      out << "        proxy_pass http://127.0.0.1:" << cfg.httpPort << ";\n";
      out << "        proxy_http_version 1.1;\n\n";

      out << "        proxy_set_header Connection \"\";\n";
      out << "        proxy_set_header Host $host;\n";
      out << "        proxy_set_header X-Real-IP $remote_addr;\n";
      out << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n";
      out << "        proxy_set_header X-Forwarded-Proto $scheme;\n\n";

      out << "        proxy_connect_timeout 10s;\n";
      out << "        proxy_send_timeout 60s;\n";
      out << "        proxy_read_timeout 60s;\n";
      out << "    }\n\n";

      if (cfg.websocketEnabled)
        out << websocket_location(cfg);

      out << "}\n";

      return out.str();
    }

    std::string render_plain_http_config(const NginxProxyConfig &cfg)
    {
      std::ostringstream out;

      out << "server {\n";
      out << "    listen 80;\n";
      out << "    listen [::]:80;\n";
      out << "    server_name " << cfg.domain << ";\n\n";

      out << "    location / {\n";
      out << "        proxy_pass http://127.0.0.1:" << cfg.httpPort << ";\n";
      out << "        proxy_http_version 1.1;\n\n";

      out << "        proxy_set_header Connection \"\";\n";
      out << "        proxy_set_header Host $host;\n";
      out << "        proxy_set_header X-Real-IP $remote_addr;\n";
      out << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n";
      out << "        proxy_set_header X-Forwarded-Proto $scheme;\n\n";

      out << "        proxy_connect_timeout 10s;\n";
      out << "        proxy_send_timeout 60s;\n";
      out << "        proxy_read_timeout 60s;\n";
      out << "    }\n\n";

      if (cfg.websocketEnabled)
        out << websocket_location(cfg);

      out << "}\n";

      return out.str();
    }
  }

  std::string render_config(const NginxProxyConfig &cfg)
  {
    if (cfg.tlsEnabled)
      return render_tls_config(cfg);

    return render_plain_http_config(cfg);
  }

  int init(const NginxProxyConfig &cfg)
  {
    nginx_output::print_init_summary(std::cout, cfg);

    if (cfg.domain.empty())
    {
      nginx_output::error(std::cerr, "Missing proxy domain.");
      nginx_output::fix(std::cerr, "add production.proxy.domain to vix.json");
      return 1;
    }

    if (cfg.tlsEnabled &&
        (cfg.certificatePath.empty() || cfg.certificateKeyPath.empty()))
    {
      nginx_output::error(std::cerr, "Missing TLS certificate paths.");
      nginx_output::fix(std::cerr, "add production.proxy.tls.certificate and production.proxy.tls.certificate_key to vix.json");
      return 1;
    }

    const std::string config = render_config(cfg);
    const fs::path tmp = fs::temp_directory_path() / cfg.appName;

    {
      std::ofstream out(tmp);

      if (!out)
      {
        nginx_output::error(
            std::cerr,
            "Failed to write temporary Nginx config: " + tmp.string());

        return 1;
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
      return 1;
    }

    run_cmd("rm -f " + shell_quote(tmp.string()));

    if (!run_cmd(
            "sudo ln -sfn " +
            shell_quote(cfg.sitesAvailablePath.string()) +
            " " +
            shell_quote(cfg.sitesEnabledPath.string())))
    {
      nginx_output::error(std::cerr, "Failed to enable Nginx site.");
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
    nginx_output::ok(std::cout, "proxy installed: " + cfg.domain);

    return 0;
  }
}
