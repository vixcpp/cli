#ifndef VIX_PUBLISH_COMMAND_HPP
#define VIX_PUBLISH_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  struct PublishCommand
  {
    static int run(const std::vector<std::string> &args);
    static int help();
  };
}

#endif
