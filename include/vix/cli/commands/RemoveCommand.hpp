#ifndef VIX_REMOVE_COMMAND_HPP
#define VIX_REMOVE_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  struct RemoveCommand
  {
    static int run(const std::vector<std::string> &args);
    static int help();
  };
}

#endif
