#ifndef VIX_ADD_COMMAND_HPP
#define VIX_ADD_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  struct AddCommand
  {
    static int run(const std::vector<std::string> &args);
    static int help();
  };
}

#endif
