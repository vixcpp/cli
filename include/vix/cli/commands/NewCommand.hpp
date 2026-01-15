#ifndef VIX_NEW_COMMAND_HPP
#define VIX_NEW_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::NewCommand
{
  int run(const std::vector<std::string> &args);
  int help();

} // namespace Vix::Commands::NewCommand

#endif // VIX_NEW_COMMAND_HPP
