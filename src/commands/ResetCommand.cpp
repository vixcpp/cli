/**
 *
 *  @file ResetCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/ResetCommand.hpp>
#include <vix/cli/commands/CleanCommand.hpp>
#include <vix/cli/commands/InstallCommand.hpp>
#include <vix/cli/util/Ui.hpp>

#include <iostream>
#include <vector>
#include <string>

namespace vix::commands
{
  int ResetCommand::run(const std::vector<std::string> &args)
  {
    if (!args.empty())
      return help();

    vix::cli::util::section(std::cout, "Reset");
    vix::cli::util::info_line(std::cout, "cleaning project cache...");
    const int cleanRc = CleanCommand::run({});
    if (cleanRc != 0)
    {
      vix::cli::util::err_line(std::cerr, "reset failed during clean");
      return cleanRc;
    }

    vix::cli::util::one_line_spacer(std::cout);
    vix::cli::util::info_line(std::cout, "reinstalling project dependencies...");
    const int installRc = InstallCommand::run({});
    if (installRc != 0)
    {
      vix::cli::util::err_line(std::cerr, "reset failed during install");
      return installRc;
    }

    vix::cli::util::one_line_spacer(std::cout);
    vix::cli::util::ok_line(std::cout, "Project reset complete");
    return 0;
  }

  int ResetCommand::help()
  {
    std::cout
        << "vix reset\n"
        << "Reset the local project state.\n\n"

        << "Usage\n"
        << "  vix reset\n\n"

        << "What it does\n"
        << "  • Runs 'vix clean'\n"
        << "  • Runs 'vix install'\n\n"

        << "Notes\n"
        << "  • Only affects the current project\n"
        << "  • Does NOT remove the global Vix directory (~/.vix)\n";

    return 0;
  }
}
