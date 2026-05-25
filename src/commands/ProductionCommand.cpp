/**
 *
 *  @file ProductionCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/ProductionCommand.hpp>
#include <vix/cli/commands/production/ProductionOutput.hpp>
#include <vix/cli/commands/production/ProductionValidator.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace vix::commands
{
  int ProductionCommand::run(const std::vector<std::string> &args)
  {
#ifndef __linux__
    production::output::error(
        std::cerr,
        "vix production is currently supported on Linux only.");

    return 1;
#else
    if (args.empty() || args[0] == "-h" || args[0] == "--help")
      return help();

    const std::string action = args[0];

    if (action == "status")
      return production::validator::status();

    if (action == "validate")
      return production::validator::validate();

    production::output::error(
        std::cerr,
        "unknown production command: " + action);

    production::output::fix(
        std::cerr,
        "vix production --help");

    return 1;
#endif
  }

  int ProductionCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix production <command>\n\n"
        << "Commands:\n"
        << "  status      Show production service, health, proxy and logs status\n"
        << "  validate    Validate production config using deploy, proxy and health checks\n\n"
        << "Examples:\n"
        << "  vix production status\n"
        << "  vix production validate\n";

    return 0;
  }
}
