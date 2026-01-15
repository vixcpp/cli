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
    (void)args;
    return repl_flow_run();
  }

  int help()
  {
    std::cout
        << "Usage:\n"
        << "  vix repl\n\n"
        << "Description:\n"
        << "  Start an interactive Vix REPL (CLI shell + calculator).\n\n"
        << "Examples:\n"
        << "  vix repl\n"
        << "  vix repl   # then: = 1+2*3\n";
    return 0;
  }
}
