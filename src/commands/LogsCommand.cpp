/**
 *
 *  @file LogsCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/LogsCommand.hpp>
#include <vix/cli/commands/logs/LogsConfig.hpp>
#include <vix/cli/commands/logs/LogsOutput.hpp>
#include <vix/cli/commands/logs/LogsRunner.hpp>
#include <vix/cli/commands/logs/LogsTypes.hpp>

#include <cstdlib>
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

    bool consume_value(
        std::vector<std::string> &args,
        const std::string &flag,
        std::string &out)
    {
      for (auto it = args.begin(); it != args.end(); ++it)
      {
        if (*it != flag)
          continue;

        auto valueIt = it + 1;

        if (valueIt == args.end())
          return false;

        out = *valueIt;
        args.erase(it, valueIt + 1);
        return true;
      }

      return true;
    }

    bool consume_int_value(
        std::vector<std::string> &args,
        const std::string &flag,
        int &out)
    {
      std::string value;

      if (!consume_value(args, flag, value))
        return false;

      if (value.empty())
        return true;

      try
      {
        const int parsed = std::stoi(value);

        if (parsed <= 0)
          return false;

        out = parsed;
        return true;
      }
      catch (...)
      {
        return false;
      }
    }

    bool parse_target(
        std::vector<std::string> &args,
        logs::LogsOptions &options)
    {
      if (args.empty())
        return true;

      const std::string target = args[0];

      if (target == "app")
      {
        options.target = logs::LogsTarget::App;
        args.erase(args.begin());
        return true;
      }

      if (target == "proxy")
      {
        options.target = logs::LogsTarget::Proxy;
        args.erase(args.begin());
        return true;
      }

      if (target == "errors")
      {
        options.target = logs::LogsTarget::Errors;
        options.errorsOnly = true;
        args.erase(args.begin());
        return true;
      }

      return true;
    }

    logs::LogsOptions parse_options(
        std::vector<std::string> &args,
        bool &ok,
        std::string &errorMessage)
    {
      logs::LogsOptions options;

      ok = true;

      if (!parse_target(args, options))
      {
        ok = false;
        errorMessage = "invalid logs target";
        return options;
      }

      options.follow = consume_flag(args, "--follow") ||
                       consume_flag(args, "-f");

      options.errorsOnly = options.errorsOnly ||
                           consume_flag(args, "--errors");

      if (!consume_value(args, "--since", options.since))
      {
        ok = false;
        errorMessage = "missing value for --since";
        return options;
      }

      if (!consume_int_value(args, "--lines", options.lines))
      {
        ok = false;
        errorMessage = "invalid value for --lines";
        return options;
      }

      if (!consume_int_value(args, "-n", options.lines))
      {
        ok = false;
        errorMessage = "invalid value for -n";
        return options;
      }

      return options;
    }
  }

  int LogsCommand::run(const std::vector<std::string> &argsIn)
  {
#ifndef __linux__
    logs::output::error(
        std::cerr,
        "vix logs is currently supported on Linux only.");

    return 1;
#else
    std::vector<std::string> args = argsIn;

    if (!args.empty() &&
        (args[0] == "-h" || args[0] == "--help"))
    {
      return help();
    }

    bool ok = true;
    std::string errorMessage;

    logs::LogsOptions options =
        parse_options(args, ok, errorMessage);

    if (!ok)
    {
      logs::output::error(std::cerr, errorMessage);
      logs::output::fix(std::cerr, "vix logs --help");
      return 1;
    }

    if (!args.empty())
    {
      logs::output::error(
          std::cerr,
          "unknown logs argument: " + args[0]);

      logs::output::fix(
          std::cerr,
          "vix logs --help");

      return 1;
    }

    logs::LogsConfig cfg =
        logs::apply_logs_options(
            logs::load_logs_config(),
            options);

    return logs::runner::run(cfg, options);
#endif
  }

  int LogsCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix logs [target] [options]\n\n"
        << "Targets:\n"
        << "  app         Show systemd app logs\n"
        << "  proxy       Show Nginx access and error logs\n"
        << "  errors      Show app errors and Nginx error logs\n\n"
        << "Options:\n"
        << "  --follow    Follow logs live\n"
        << "  -f          Alias for --follow\n"
        << "  --errors    Filter logs by common error keywords\n"
        << "  --since     Filter app logs by systemd time expression\n"
        << "  --lines     Show last N lines\n"
        << "  -n          Alias for --lines\n"
        << "  -h, --help  Show this help message\n\n"
        << "Examples:\n"
        << "  vix logs\n"
        << "  vix logs app\n"
        << "  vix logs proxy\n"
        << "  vix logs errors\n"
        << "  vix logs --follow\n"
        << "  vix logs --errors\n"
        << "  vix logs --since \"1 hour ago\"\n"
        << "  vix logs -n 200\n\n"
        << "Config:\n"
        << "  production.logs.service\n"
        << "  production.logs.nginx_access\n"
        << "  production.logs.nginx_error\n"
        << "  production.logs.lines\n";

    return 0;
  }
}
