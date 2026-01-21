#ifndef VIX_MODULES_COMMAND_HPP
#define VIX_MODULES_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::ModulesCommand
{
  int run(const std::vector<std::string> &args);
  int help();
}

#endif
