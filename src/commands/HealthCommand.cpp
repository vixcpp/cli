/**
 *
 *  @file HealthCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/HealthCommand.hpp>
#include <vix/cli/commands/health/HealthChecker.hpp>
#include <vix/cli/commands/health/HealthConfig.hpp>
#include <vix/cli/commands/health/HealthOutput.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace vix::commands
{
  int HealthCommand::run(const std::vector<std::string> &args)
  {
    if (args.empty())
    {
      const auto cfg = health::load_health_config();
      return health::checker::check_all(cfg);
    }

    if (args[0] == "-h" || args[0] == "--help")
      return help();

    const auto cfg = health::load_health_config();
    const std::string action = args[0];

    if (action == "local")
      return health::checker::check_local(cfg);

    if (action == "public")
      return health::checker::check_public(cfg);

    if (action == "websocket" || action == "ws")
      return health::checker::check_websocket(cfg);

    health::output::error(
        std::cerr,
        "unknown health command: " + action);

    health::output::fix(
        std::cerr,
        "vix health --help");

    return 1;
  }

  int HealthCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix health [command]\n\n"
        << "Commands:\n"
        << "  local       Check the local application endpoint\n"
        << "  public      Check the public HTTPS endpoint\n"
        << "  websocket   Check the WebSocket endpoint\n"
        << "  ws          Alias for websocket\n\n"
        << "Examples:\n"
        << "  vix health\n"
        << "  vix health local\n"
        << "  vix health public\n"
        << "  vix health websocket\n";

    return 0;
  }
}
