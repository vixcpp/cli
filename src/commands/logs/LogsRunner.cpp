/**
 *
 *  @file LogsRunner.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/logs/LogsRunner.hpp>
#include <vix/cli/commands/logs/LogsOutput.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace vix::commands::logs::runner
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
      output::command(std::cout, cmd);
      return std::system(cmd.c_str()) == 0;
    }

    std::string grep_errors_command()
    {
      return "grep -Ei 'error|failed|failure|exception|panic|fatal|critical|timeout|refused|denied'";
    }

    std::string build_journalctl_command(
        const LogsConfig &cfg,
        const LogsOptions &options)
    {
      std::string cmd =
          "journalctl -u " +
          shell_quote(cfg.serviceName);

      if (!options.since.empty())
      {
        cmd += " --since ";
        cmd += shell_quote(options.since);
      }

      if (options.follow)
      {
        cmd += " -f";
      }
      else
      {
        cmd += " -n ";
        cmd += std::to_string(cfg.lines);
        cmd += " --no-pager";
      }

      if (options.errorsOnly ||
          options.target == LogsTarget::Errors)
      {
        cmd += " | ";
        cmd += grep_errors_command();
      }

      return cmd;
    }

    std::string build_tail_command(
        const fs::path &path,
        const LogsConfig &cfg,
        const LogsOptions &options,
        bool errorsOnly)
    {
      std::string cmd = "sudo tail ";

      if (options.follow)
      {
        cmd += "-f ";
      }
      else
      {
        cmd += "-n ";
        cmd += std::to_string(cfg.lines);
        cmd += " ";
      }

      cmd += shell_quote(path.string());

      if (errorsOnly)
      {
        cmd += " | ";
        cmd += grep_errors_command();
      }

      return cmd;
    }

    bool show_app_logs(
        const LogsConfig &cfg,
        const LogsOptions &options)
    {
      if (cfg.serviceName.empty())
      {
        output::error(std::cerr, "Missing service name for app logs.");
        output::fix(std::cerr, "add production.logs.service to vix.json");
        return false;
      }

      output::step(std::cout, "App Logs");

      return run_cmd(build_journalctl_command(cfg, options));
    }

    bool show_proxy_access_logs(
        const LogsConfig &cfg,
        const LogsOptions &options)
    {
      if (!fs::exists(cfg.nginxAccessLog))
      {
        output::warn(
            std::cerr,
            "Nginx access log not found: " + cfg.nginxAccessLog.string());

        output::fix(
            std::cerr,
            "check production.logs.nginx_access in vix.json");

        return false;
      }

      output::step(std::cout, "Proxy Access Logs");

      return run_cmd(
          build_tail_command(
              cfg.nginxAccessLog,
              cfg,
              options,
              options.errorsOnly));
    }

    bool show_proxy_error_logs(
        const LogsConfig &cfg,
        const LogsOptions &options)
    {
      if (!fs::exists(cfg.nginxErrorLog))
      {
        output::warn(
            std::cerr,
            "Nginx error log not found: " + cfg.nginxErrorLog.string());

        output::fix(
            std::cerr,
            "check production.logs.nginx_error in vix.json");

        return false;
      }

      output::step(std::cout, "Proxy Error Logs");

      return run_cmd(
          build_tail_command(
              cfg.nginxErrorLog,
              cfg,
              options,
              false));
    }

    bool show_proxy_logs(
        const LogsConfig &cfg,
        const LogsOptions &options)
    {
      bool ok = true;

      if (!options.errorsOnly &&
          options.target != LogsTarget::Errors)
      {
        if (!show_proxy_access_logs(cfg, options))
          ok = false;
      }

      if (!show_proxy_error_logs(cfg, options))
        ok = false;

      return ok;
    }

    bool show_error_logs(
        const LogsConfig &cfg,
        LogsOptions options)
    {
      options.errorsOnly = true;

      bool ok = true;

      if (!show_app_logs(cfg, options))
        ok = false;

      if (!show_proxy_error_logs(cfg, options))
        ok = false;

      return ok;
    }
  }

  int run(
      const LogsConfig &cfg,
      const LogsOptions &options)
  {
    output::print_summary(std::cout, cfg, options);

    bool ok = true;

    switch (options.target)
    {
    case LogsTarget::App:
      ok = show_app_logs(cfg, options);
      break;

    case LogsTarget::Proxy:
      ok = show_proxy_logs(cfg, options);
      break;

    case LogsTarget::Errors:
      ok = show_error_logs(cfg, options);
      break;

    case LogsTarget::All:
      if (!show_app_logs(cfg, options))
        ok = false;

      if (!show_proxy_logs(cfg, options))
        ok = false;

      break;
    }

    if (!ok)
      return 1;

    output::ok(std::cout, "logs command completed");
    return 0;
  }
}
