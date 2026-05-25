/**
 *
 *  @file DeployCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/DeployCommand.hpp>
#include <vix/cli/commands/deploy/DeployConfig.hpp>
#include <vix/cli/commands/deploy/DeployOutput.hpp>
#include <vix/cli/commands/deploy/DeployRunner.hpp>
#include <vix/cli/commands/deploy/DeployTypes.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace vix::commands
{
  namespace
  {
    bool consume_flag(
        std::vector<std::string> &args,
        const std::string &flag)
    {
      for (auto it = args.begin(); it != args.end(); ++it)
      {
        if (*it == flag)
        {
          args.erase(it);
          return true;
        }
      }

      return false;
    }

    deploy::DeployOptions parse_options(
        std::vector<std::string> &args)
    {
      deploy::DeployOptions options;

      options.dryRun = consume_flag(args, "--dry-run");
      options.verbose = consume_flag(args, "--verbose") ||
                        consume_flag(args, "-v");
      options.noPull = consume_flag(args, "--no-pull");
      options.noTests = consume_flag(args, "--no-tests");

      return options;
    }
  }

  int DeployCommand::run(const std::vector<std::string> &argsIn)
  {
#ifndef __linux__
    deploy::output::error(
        std::cerr,
        "vix deploy is currently supported on Linux only.");

    return 1;
#else
    std::vector<std::string> args = argsIn;

    if (!args.empty() &&
        (args[0] == "-h" || args[0] == "--help"))
    {
      return help();
    }

    deploy::DeployOptions options = parse_options(args);

    if (!args.empty())
    {
      deploy::output::error(
          std::cerr,
          "unknown deploy argument: " + args[0]);

      deploy::output::fix(
          std::cerr,
          "vix deploy --help");

      return 1;
    }

    deploy::DeployConfig cfg =
        deploy::apply_deploy_options(
            deploy::load_deploy_config(),
            options);

    return deploy::runner::run(cfg, options);
#endif
  }

  int DeployCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix deploy [options]\n\n"
        << "Options:\n"
        << "  --dry-run     Print deployment steps without executing them\n"
        << "  --verbose     Print additional execution details\n"
        << "  -v            Alias for --verbose\n"
        << "  --no-pull     Skip git pull even when enabled in vix.json\n"
        << "  --no-tests    Skip tests even when enabled in vix.json\n"
        << "  -h, --help    Show this help message\n\n"
        << "Examples:\n"
        << "  vix deploy\n"
        << "  vix deploy --dry-run\n"
        << "  vix deploy --no-pull\n"
        << "  vix deploy --verbose\n\n"
        << "Config:\n"
        << "  production.deploy.pull\n"
        << "  production.deploy.branch\n"
        << "  production.deploy.build\n"
        << "  production.deploy.tests\n"
        << "  production.deploy.test_command\n"
        << "  production.deploy.service\n"
        << "  production.deploy.health_local\n"
        << "  production.deploy.health_public\n"
        << "  production.deploy.proxy_check\n"
        << "  production.deploy.proxy_reload\n"
        << "  production.deploy.logs_on_failure\n"
        << "  production.deploy.log_lines\n"
        << "  production.deploy.rollback\n"
        << "  production.health.service\n"
        << "  production.health.local\n"
        << "  production.health.public\n"
        << "  production.health.websocket\n"
        << "  production.logs.service\n"
        << "  production.logs.nginx_access\n"
        << "  production.logs.nginx_error\n";

    return 0;
  }
}
