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
#include <array>
#include <cstdio>
#include <memory>
#include <optional>

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

    std::optional<std::string> run_capture(const std::string &cmd)
    {
      std::array<char, 2048> buffer{};
      std::string output;

      std::unique_ptr<FILE, decltype(&pclose)> pipe(
          popen(cmd.c_str(), "r"),
          pclose);

      if (!pipe)
        return std::nullopt;

      while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr)
        output += buffer.data();

      while (!output.empty() &&
             (output.back() == '\n' ||
              output.back() == '\r' ||
              output.back() == ' ' ||
              output.back() == '\t'))
      {
        output.pop_back();
      }

      if (output.empty())
        return std::nullopt;

      return output;
    }

    struct DeployState
    {
      std::string previousGitRef{};
    };

    int fail(
        const DeployConfig &cfg,
        const DeployOptions &options,
        const std::string &message,
        const std::string &fixMessage = {})
    {
      output::error(std::cerr, message);

      if (!fixMessage.empty())
        output::fix(std::cerr, fixMessage);

      if (cfg.logsOnFailure)
      {
        const std::string logsCmd =
            "vix logs errors --repeated -n " +
            std::to_string(cfg.logLines);

        output::step(std::cout, "Failure Logs");
        output::command(std::cout, logsCmd);

        if (!options.dryRun)
          (void)std::system(logsCmd.c_str());
      }

      return 1;
    }

    bool rollback_deploy(
        const DeployConfig &cfg,
        const DeployOptions &options,
        const DeployState &state)
    {
      if (!cfg.rollback)
        return true;

      if (state.previousGitRef.empty())
      {
        output::warn(std::cerr, "rollback skipped: no previous git revision captured");
        return false;
      }

      output::step(std::cout, "Rollback");

      const std::string resetCmd =
          "git reset --hard " +
          shell_quote(state.previousGitRef);

      if (!run_cmd(resetCmd, options))
        return false;

      if (!run_cmd(cfg.buildCommand, options))
        return false;

      if (!run_cmd("vix service restart", options))
        return false;

      if (!run_cmd("vix service status", options))
        return false;

      output::ok(std::cout, "rollback completed");
      return true;
    }

    int fail_with_rollback(
        const DeployConfig &cfg,
        const DeployOptions &options,
        const DeployState &state,
        const std::string &message,
        const std::string &fixMessage = {})
    {
      output::error(std::cerr, message);

      if (!fixMessage.empty())
        output::fix(std::cerr, fixMessage);

      if (cfg.rollback)
      {
        if (!rollback_deploy(cfg, options, state))
        {
          output::error(std::cerr, "rollback failed");
          output::fix(std::cerr, "inspect the repository and service manually");
        }
      }

      if (cfg.logsOnFailure)
      {
        const std::string logsCmd =
            "vix logs errors --repeated -n " +
            std::to_string(cfg.logLines);

        output::step(std::cout, "Failure Logs");
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

      return run_cmd("vix service restart", options);
    }

    bool verify_service(
        const DeployConfig &cfg,
        const DeployOptions &options)
    {
      if (cfg.serviceName.empty())
        return false;

      output::step(std::cout, "Service Status");

      return run_cmd("vix service status", options);
    }

    bool health_check(
        const DeployConfig &cfg,
        const DeployOptions &options)
    {
      if (!cfg.healthLocal && !cfg.healthPublic)
        return true;

      output::step(std::cout, "Health Check");

      return run_cmd("vix health", options);
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

    DeployState state;

    if (cfg.rollback && !options.dryRun)
    {
      const auto ref = run_capture("git rev-parse HEAD 2>/dev/null");

      if (ref)
        state.previousGitRef = *ref;
      else
        output::warn(std::cerr, "rollback enabled but current git revision could not be captured");
    }

    if (cfg.serviceName.empty())
    {
      output::error(std::cerr, "Missing deployment service name.");
      output::fix(std::cerr, "add production.deploy.service to vix.json");
      return 1;
    }

    if (!pull_latest(cfg, options))
    {
      return fail_with_rollback(
          cfg,
          options,
          state,
          "git pull failed",
          "check repository state and branch");
    }

    if (cfg.pull)
      output::ok(std::cout, "git pull completed");

    if (!build_app(cfg, options))
    {
      return fail_with_rollback(
          cfg,
          options,
          state,
          "build failed",
          "run the configured build command manually");
    }

    output::ok(std::cout, "build completed");

    if (!run_tests(cfg, options))
    {
      return fail_with_rollback(
          cfg,
          options,
          state,
          "tests failed",
          "run the configured test command manually");
    }

    if (cfg.tests)
      output::ok(std::cout, "tests completed");

    if (!restart_service(cfg, options))
    {
      return fail_with_rollback(
          cfg,
          options,
          state,
          "service restart failed",
          "run `vix service restart`");
    }

    output::ok(std::cout, "service restarted");

    if (!verify_service(cfg, options))
    {
      return fail_with_rollback(
          cfg,
          options,
          state,
          "service is not active",
          "run `vix service status`");
    }

    output::ok(std::cout, "service is active");

    if (!health_check(cfg, options))
    {
      return fail_with_rollback(
          cfg,
          options,
          state,
          "health check failed",
          "run `vix health`");
    }

    if (cfg.healthLocal || cfg.healthPublic)
      output::ok(std::cout, "health checks passed");

    if (!proxy_check(cfg, options))
    {
      return fail_with_rollback(
          cfg,
          options,
          state,
          "proxy check failed",
          "run `vix proxy nginx check`");
    }

    if (cfg.proxyCheck || cfg.proxyReload)
      output::ok(std::cout, "proxy config is valid");

    if (!proxy_reload(cfg, options))
    {
      return fail_with_rollback(
          cfg,
          options,
          state,
          "proxy reload failed",
          "run `vix proxy nginx reload`");
    }

    if (cfg.proxyReload)
      output::ok(std::cout, "proxy reloaded");

    output::ok(std::cout, "deployment completed");

    return 0;
  }
}
