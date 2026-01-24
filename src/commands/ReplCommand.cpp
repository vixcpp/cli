/**
 *
 *  @file ReplCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/ReplCommand.hpp>
#include <vix/cli/commands/repl/ReplDetail.hpp>
#include <vix/cli/commands/repl/ReplFlow.hpp>
#include <iostream>

namespace vix::commands::ReplCommand
{
  int run(const std::vector<std::string> &args)
  {
    std::vector<std::string> replArgs = args;
    if (!replArgs.empty() && replArgs[0] == "--")
      replArgs.erase(replArgs.begin());

    return repl_flow_run(replArgs);
  }

  int help()
  {
    std::cout
        << "Usage:\n"
        << "  vix repl [-- <args...>]\n\n"
        << "Description:\n"
        << "  Start an interactive Vix REPL (shell + calculator).\n"
        << "  Args after `--` are available via Vix.args().\n\n"
        << "Examples:\n"
        << "  vix repl\n"
        << "  vix repl -- --port 8080 --mode dev\n"
        << "  # inside REPL:\n"
        << "  #   Vix.args()\n"
        << "  #   x = 1+2*3\n";
    return 0;
  }

}
