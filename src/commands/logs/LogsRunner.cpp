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
#include <vix/cli/commands/logs/LogsAnalyzer.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <array>
#include <cstdio>
#include <memory>
#include <vector>

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

#ifdef __linux__

    std::string grep_errors_command()
    {
      return "grep -Ei 'error|failed|failure|exception|panic|fatal|critical|timeout|refused|denied'";
    }

    std::string grep_analysis_command()
    {
      return "grep -Ei 'error|failed|failure|exception|panic|fatal|critical|timeout|refused|denied|disconnect|disconnected|closed|reset by peer|broken pipe|eof'";
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

    std::string build_journalctl_analysis_command(
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

      cmd += " -n ";
      cmd += std::to_string(cfg.lines);
      cmd += " --no-pager";

      cmd += " | ";
      cmd += grep_analysis_command();

      return cmd;
    }

    std::vector<std::string> read_command_lines(
        const std::string &cmd)
    {
      std::vector<std::string> lines;
      std::array<char, 4096> buffer{};

      std::unique_ptr<FILE, decltype(&pclose)> pipe(
          popen(cmd.c_str(), "r"),
          pclose);

      if (!pipe)
        return lines;

      while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr)
      {
        std::string line = buffer.data();

        while (!line.empty() &&
               (line.back() == '\n' || line.back() == '\r'))
        {
          line.pop_back();
        }

        if (!line.empty())
          lines.push_back(line);
      }

      return lines;
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

    bool show_repeated_errors(
        const LogsConfig &cfg,
        LogsOptions options)
    {
      options.errorsOnly = true;
      options.follow = false;

      std::vector<std::string> lines;

      if (!cfg.serviceName.empty())
      {
        const std::string appCommand =
            build_journalctl_analysis_command(cfg, options);

        if (!options.json)
        {
          output::step(std::cout, "Analyze App Errors");
          output::ok(std::cout, "reading systemd app errors");
        }

        std::vector<std::string> appLines =
            read_command_lines(appCommand);

        lines.insert(
            lines.end(),
            appLines.begin(),
            appLines.end());
      }

      if (fs::exists(cfg.nginxErrorLog))
      {
        const std::string proxyCommand =
            build_tail_command(
                cfg.nginxErrorLog,
                cfg,
                options,
                false);

        if (!options.json)
        {
          output::step(std::cout, "Analyze Proxy Errors");
          output::ok(std::cout, "reading Nginx proxy errors");
        }

        std::vector<std::string> proxyLines =
            read_command_lines(proxyCommand);

        lines.insert(
            lines.end(),
            proxyLines.begin(),
            proxyLines.end());
      }
      else if (!options.json)
      {
        output::warn(
            std::cerr,
            "Nginx error log not found: " + cfg.nginxErrorLog.string());

        output::fix(
            std::cerr,
            "add production.logs.nginx_error to vix.json or create the Nginx log file");
      }

      const analyzer::RepeatedLogReport report =
          analyzer::analyze_repeated_errors(lines);

      if (options.json)
        analyzer::print_repeated_report_json(std::cout, report);
      else
        analyzer::print_repeated_report(std::cout, report);

      return true;
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

#endif // __linux__
  }

  int run(
      const LogsConfig &cfg,
      const LogsOptions &options)
  {
#ifndef __linux__
    (void)cfg;
    (void)options;

    output::error(
        std::cerr,
        "vix logs is currently supported on Linux only.");

    output::fix(
        std::cerr,
        "run this command on the Linux server that hosts the service");

    return 1;
#else
    if (!options.json)
      output::print_summary(std::cout, cfg, options);

    if (options.repeated)
      return show_repeated_errors(cfg, options) ? 0 : 1;

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
#endif
  }
}
