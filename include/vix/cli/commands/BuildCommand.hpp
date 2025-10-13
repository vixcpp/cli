#ifndef VIX_BUILD_COMMAND_HPP
#define VIX_BUILD_COMMAND_HPP

#include <string>
#include <vector>

namespace Vix::Commands::BuildCommand
{
    int run(const std::vector<std::string> &args);
}

#endif // VIX_BUILD_COMMAND_HPP
