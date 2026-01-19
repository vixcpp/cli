#ifndef VIX_REGISTRY_COMMAND_HPP
#define VIX_REGISTRY_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  struct RegistryCommand
  {
    static int run(const std::vector<std::string> &args);
    static int help();
  };
}

#endif
