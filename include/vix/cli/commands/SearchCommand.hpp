#ifndef VIX_SEARCH_COMMAND_HPP
#define VIX_SEARCH_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  struct SearchCommand
  {
    static int run(const std::vector<std::string> &args);
    static int help();
  };
}

#endif
