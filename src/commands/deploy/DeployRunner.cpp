/**
 *
 *  @file DeployRunner.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/deploy/DeployRunner.hpp>
#include <vix/cli/commands/deploy/DeployOutput.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace vix::commands::deploy::runner
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

    bool run_cmd(
        const std::string &cmd,
        const DeployOptions &options)
    {
      output::command(std::cout, cmd);

      if (options.dryRun)
        return true;

      return std::system(cmd.c_str()) == 0;
    }

    int fail(
        const DeployConfig &cfg,
        const DeployOptions &options,
        const std::string &message,
        const std::string &fixMessage = {})
    {
      output::error(std::cerr, message);

      if (!fixMessage.empty())
        output::fix(std::cerr, fixMessage);

      if (cfg.logsOnFailure && !cfg.serviceName.empty())
      {
        const std::string logsCmd =
            "sudo journalctl -u " +
            shell_quote(cfg.serviceName) +
            " -n " +
            std::to_string(cfg.logLines) +
            " --no-pager";

        output::step(std::cout, "Last Logs");
        output::command(std::cout, logsCmd);

        if (!options.dryRun)
          (void)std::system(logsCmd.c_str());
      }

      return 1;
    }

    bool pull_latest(
        const DeployConfig &cfg,
        const DeployOptions &options)
    {
      if (!cfg.pull)
        return true;

      output::step(std::cout, "Pull");

      const std::string cmd =
          "git pull origin " + shell_quote(cfg.branch);

      return run_cmd(cmd, options);
    }

    bool build_app(
        const DeployConfig &cfg,
        const DeployOptions &options)
    {
      output::step(std::cout, "Build");

      return run_cmd(cfg.buildCommand, options);
    }

    bool run_tests(
        const DeployConfig &cfg,
        const DeployOptions &options)
    {
      if (!cfg.tests)
        return true;

      output::step(std::cout, "Tests");

      return run_cmd(cfg.testCommand, options);
    }

    bool restart_service(
        const DeployConfig &cfg,
        const DeployOptions &options)
    {
      if (cfg.serviceName.empty())
        return false;

      output::step(std::cout, "Restart Service");

      const std::string cmd =
          "sudo systemctl restart " +
          shell_quote(cfg.serviceName);

      return run_cmd(cmd, options);
    }

    bool verify_service(
        const DeployConfig &cfg,
        const DeployOptions &options)
    {
      if (cfg.serviceName.empty())
        return false;

      output::step(std::cout, "Service Status");

      const std::string cmd =
          "systemctl is-active --quiet " +
          shell_quote(cfg.serviceName);

      return run_cmd(cmd, options);
    }

    bool service_health(
        const DeployConfig &cfg,
        const DeployOptions &options)
    {
      if (!cfg.healthLocal && !cfg.healthPublic)
        return true;

      output::step(std::cout, "Service Health");

      return run_cmd("vix service health", options);
    }

    bool proxy_check(
        const DeployConfig &cfg,
        const DeployOptions &options)
    {
      if (!cfg.proxyCheck && !cfg.proxyReload)
        return true;

      output::step(std::cout, "Proxy Check");

      return run_cmd("vix proxy nginx check", options);
    }

    bool proxy_reload(
        const DeployConfig &cfg,
        const DeployOptions &options)
    {
      if (!cfg.proxyReload)
        return true;

      output::step(std::cout, "Proxy Reload");

      return run_cmd("vix proxy nginx reload", options);
    }
  }

  int run(
      const DeployConfig &cfg,
      const DeployOptions &options)
  {
    output::print_summary(std::cout, cfg, options);

    if (cfg.serviceName.empty())
    {
      output::error(std::cerr, "Missing deployment service name.");
      output::fix(std::cerr, "add production.deploy.service to vix.json");
      return 1;
    }

    if (!pull_latest(cfg, options))
      return fail(cfg, options, "git pull failed", "check repository state and branch");

    if (cfg.pull)
      output::ok(std::cout, "git pull completed");

    if (!build_app(cfg, options))
      return fail(cfg, options, "build failed", "run the configured build command manually");

    output::ok(std::cout, "build completed");

    if (!run_tests(cfg, options))
      return fail(cfg, options, "tests failed", "run the configured test command manually");

    if (cfg.tests)
      output::ok(std::cout, "tests completed");

    if (!restart_service(cfg, options))
      return fail(cfg, options, "service restart failed", "run `vix service restart`");

    output::ok(std::cout, "service restarted");

    if (!verify_service(cfg, options))
      return fail(cfg, options, "service is not active", "run `vix service status`");

    output::ok(std::cout, "service is active");

    if (!service_health(cfg, options))
      return fail(cfg, options, "service health check failed", "run `vix service health`");

    if (cfg.healthLocal || cfg.healthPublic)
      output::ok(std::cout, "service health checks passed");

    if (!proxy_check(cfg, options))
      return fail(cfg, options, "proxy check failed", "run `vix proxy nginx check`");

    if (cfg.proxyCheck || cfg.proxyReload)
      output::ok(std::cout, "proxy config is valid");

    if (!proxy_reload(cfg, options))
      return fail(cfg, options, "proxy reload failed", "run `vix proxy nginx reload`");

    if (cfg.proxyReload)
      output::ok(std::cout, "proxy reloaded");

    output::ok(std::cout, "deployment completed");

    return 0;
  }
}
