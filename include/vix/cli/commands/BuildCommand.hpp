#ifndef VIX_BUILD_COMMAND_HPP
#define VIX_BUILD_COMMAND_HPP

#include <vector>
#include <string>

namespace Vix::Commands::BuildCommand
{
    int run(const std::vector<std::string> &args);
}

#endif