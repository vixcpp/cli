#ifndef VIX_REPL_COMMAND_HPP
#define VIX_REPL_COMMAND_HPP

#include <vector>
#include <string>

namespace vix::commands::ReplCommand
{
  int run(const std::vector<std::string> &args);
  int help();
}

#endif
