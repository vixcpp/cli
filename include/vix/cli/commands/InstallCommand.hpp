#ifndef VIX_INSTALL_COMMAND_HPP
#define VIX_INSTALL_COMMAND_HPP

#include <vector>
#include <string>

namespace vix::commands::InstallCommand
{
  int run(const std::vector<std::string> &args);
  int help();
}

#endif
