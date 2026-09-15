/**
 *
 *  @file MobileCommandUnavailable.cpp
 *
 *  Fallback implementation used when vix::ui is not part of the build graph.
 *
 */

#include <vix/cli/commands/MobileCommand.hpp>

#include <iostream>

namespace vix::commands
{
  int MobileCommand::run(const std::vector<std::string> &)
  {
    std::cerr << "The mobile command is unavailable in this build.\n"
              << "Rebuild vix with the vix::ui module available.\n";
    return 1;
  }

  int MobileCommand::help()
  {
    std::cout << "The mobile command is unavailable in this build.\n"
              << "Rebuild vix with the vix::ui module available.\n";
    return 1;
  }
}
