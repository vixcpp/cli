#ifndef VIX_LIST_COMMAND_HPP
#define VIX_LIST_COMMAND_HPP

#include <vector>
#include <string>

namespace vix::commands
{
  struct ListCommand
  {
    static int run(const std::vector<std::string> &args);
    static int help();
  };
}

#endif
