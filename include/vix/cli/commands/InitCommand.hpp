/**
 * @file InitCommand.hpp
 */
#ifndef VIX_INIT_COMMAND_HPP
#define VIX_INIT_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  struct InitCommand
  {
    static int run(const std::vector<std::string> &args);
    static int help();
  };
}

#endif
