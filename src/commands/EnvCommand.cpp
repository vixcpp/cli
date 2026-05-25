/**
 *
 *  @file EnvCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/EnvCommand.hpp>
#include <vix/cli/commands/env/EnvChecker.hpp>
#include <vix/cli/commands/env/EnvConfig.hpp>
#include <vix/cli/commands/env/EnvOutput.hpp>
#include <vix/cli/commands/env/EnvTypes.hpp>

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

    env::EnvOptions parse_options(
        std::vector<std::string> &args)
    {
      env::EnvOptions options;

      options.production = consume_flag(args, "--production");

      if (consume_flag(args, "--show-values"))
        options.showValues = true;

      if (consume_flag(args, "--masked"))
        options.masked = true;

      if (consume_flag(args, "--no-mask"))
        options.masked = false;

      return options;
    }

    int run_check(std::vector<std::string> args)
    {
      env::EnvOptions options = parse_options(args);

      if (!args.empty())
      {
        env::output::error(
            std::cerr,
            "unknown env check argument: " + args[0]);

        env::output::fix(
            std::cerr,
            "vix env check --help");

        return 1;
      }

      const env::EnvConfig cfg =
          env::load_env_config(options);

      return env::checker::check(cfg, options);
    }
  }

  int EnvCommand::run(const std::vector<std::string> &argsIn)
  {
    if (argsIn.empty() ||
        argsIn[0] == "-h" ||
        argsIn[0] == "--help")
    {
      return help();
    }

    const std::string action = argsIn[0];

    if (action == "check")
    {
      std::vector<std::string> args;

      if (argsIn.size() > 1)
        args.assign(argsIn.begin() + 1, argsIn.end());

      if (!args.empty() &&
          (args[0] == "-h" || args[0] == "--help"))
      {
        return help();
      }

      return run_check(std::move(args));
    }

    env::output::error(
        std::cerr,
        "unknown env command: " + action);

    env::output::fix(
        std::cerr,
        "vix env --help");

    return 1;
  }

  int EnvCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix env <command> [options]\n\n"
        << "Commands:\n"
        << "  check          Check project environment files\n\n"
        << "Options:\n"
        << "  --production   Validate production env requirements and systemd env\n"
        << "  --masked       Mask printed values\n"
        << "  --no-mask      Do not mask non-secret values when using --show-values\n"
        << "  --show-values  Print env values; secrets are always masked\n"
        << "  -h, --help     Show this help message\n\n"
        << "Examples:\n"
        << "  vix env check\n"
        << "  vix env check --production\n"
        << "  vix env check --production --show-values\n\n"
        << "Config:\n"
        << "  .env\n"
        << "  .env.example\n"
        << "  production.env.required\n"
        << "  production.service.name\n"
        << "  production.deploy.service\n";

    return 0;
  }
}
