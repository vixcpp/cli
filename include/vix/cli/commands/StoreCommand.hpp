#ifndef VIX_STORE_COMMAND_HPP
#define VIX_STORE_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  struct StoreCommand
  {
    static int run(const std::vector<std::string> &args);
    static int help();
  };
}

#endif
