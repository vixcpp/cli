/**
 *
 *  @file ReplCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by an MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */

#include <vix/cli/commands/ReplCommand.hpp>
#include <vix/reply/core/ReplFlow.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace vix::commands::ReplCommand
{
  int run(const std::vector<std::string> &args)
  {
    if (!args.empty())
    {
      std::cerr << "Usage: vix repl\n";
      return 2;
    }
    return vix::reply::repl_flow_run({});
  }

  int help()
  {
    std::cout
        << "Usage:\n  vix repl\n\n"
        << "Start an interactive C++ REPL.\n";

    return 0;
  }
}
