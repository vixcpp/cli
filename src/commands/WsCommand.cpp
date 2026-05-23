/**
 *
 *  @file WsCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/WsCommand.hpp>
#include <vix/cli/commands/ws/WsChecker.hpp>
#include <vix/cli/commands/ws/WsConfig.hpp>
#include <vix/cli/commands/ws/WsOutput.hpp>
#include <vix/cli/commands/ws/WsTypes.hpp>

#include <cstdint>
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

    bool consume_u64_value(
        std::vector<std::string> &args,
        const std::string &flag,
        std::uint64_t &out)
    {
      std::string value;

      if (!consume_value(args, flag, value))
        return false;

      if (value.empty())
        return true;

      try
      {
        const unsigned long long parsed = std::stoull(value);

        if (parsed == 0)
          return false;

        out = static_cast<std::uint64_t>(parsed);
        return true;
      }
      catch (...)
      {
        return false;
      }
    }

    ws::WsOptions parse_options(
        std::vector<std::string> &args,
        bool &ok,
        std::string &errorMessage)
    {
      ws::WsOptions options;

      ok = true;

      options.verbose = consume_flag(args, "--verbose") ||
                        consume_flag(args, "-v");

      options.ping = !consume_flag(args, "--no-ping");

      if (!consume_u64_value(args, "--timeout", options.timeoutMs))
      {
        ok = false;
        errorMessage = "invalid value for --timeout";
        return options;
      }

      return options;
    }

    bool consume_check_target(
        std::vector<std::string> &args,
        ws::WsOptions &options,
        std::string &errorMessage)
    {
      if (args.empty())
      {
        options.target = ws::WsTarget::Check;
        return true;
      }

      const std::string action = args[0];

      if (action != "check")
      {
        errorMessage = "unknown ws command: " + action;
        return false;
      }

      options.target = ws::WsTarget::Check;
      args.erase(args.begin());

      if (!args.empty() &&
          args[0].rfind("-", 0) != 0)
      {
        options.url = args[0];
        args.erase(args.begin());
      }

      return true;
    }
  }

  int WsCommand::run(const std::vector<std::string> &argsIn)
  {
#ifndef __linux__
    ws::output::error(
        std::cerr,
        "vix ws is currently supported on Linux only.");

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

    ws::WsOptions options =
        parse_options(args, ok, errorMessage);

    if (!ok)
    {
      ws::output::error(std::cerr, errorMessage);
      ws::output::fix(std::cerr, "vix ws --help");
      return 1;
    }

    if (!consume_check_target(args, options, errorMessage))
    {
      ws::output::error(std::cerr, errorMessage);
      ws::output::fix(std::cerr, "vix ws --help");
      return 1;
    }

    if (!args.empty())
    {
      ws::output::error(
          std::cerr,
          "unknown ws argument: " + args[0]);

      ws::output::fix(
          std::cerr,
          "vix ws --help");

      return 1;
    }

    ws::WsConfig cfg =
        ws::apply_ws_options(
            ws::load_ws_config(),
            options);

    return ws::checker::check(cfg, options);
#endif
  }

  int WsCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix ws check [url] [options]\n\n"
        << "Commands:\n"
        << "  check       Check a WebSocket endpoint\n\n"
        << "Options:\n"
        << "  --timeout   Connection timeout in milliseconds\n"
        << "  --no-ping   Do not send a ping frame after handshake\n"
        << "  --verbose   Print additional diagnostics\n"
        << "  -v          Alias for --verbose\n"
        << "  -h, --help  Show this help message\n\n"
        << "Examples:\n"
        << "  vix ws check ws://127.0.0.1:9090/ws\n"
        << "  vix ws check wss://pulsegrid.softadastra.com/ws\n"
        << "  vix ws check --timeout 5000\n"
        << "  vix ws check --no-ping\n\n"
        << "Config:\n"
        << "  production.websocket.host\n"
        << "  production.websocket.port\n"
        << "  production.websocket.path\n"
        << "  production.websocket.local_url\n"
        << "  production.websocket.public_url\n"
        << "  production.websocket.timeout_ms\n"
        << "  production.websocket.heartbeat\n"
        << "  production.proxy.websocket.path\n"
        << "  production.proxy.websocket.port\n";

    return 0;
  }
}
