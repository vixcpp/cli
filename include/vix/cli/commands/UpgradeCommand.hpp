#ifndef VIX_CLI_COMMANDS_UPGRADE_COMMAND_HPP
#define VIX_CLI_COMMANDS_UPGRADE_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  struct UpgradeCommand
  {
    static int run(const std::vector<std::string> &args);
    static int help();
  };
}

#endif
