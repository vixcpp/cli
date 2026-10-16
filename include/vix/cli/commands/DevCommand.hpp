#ifndef VIX_DEV_COMMAND_HPP
#define VIX_DEV_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::DevCommand
{
  int run(const std::vector<std::string> &arguments);
  int help();
}

#endif
