/** @file TargetsCommand.hpp */
#ifndef VIX_TARGETS_COMMAND_HPP
#define VIX_TARGETS_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::TargetsCommand
{
  int run(const std::vector<std::string> &args);
  int help();
}

#endif // VIX_TARGETS_COMMAND_HPP
