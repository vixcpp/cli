#ifndef VIX_BUILD_COMMAND_HPP
#define VIX_BUILD_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::BuildCommand
{
    int run(const std::vector<std::string> &args);
}

#endif // VIX_BUILD_COMMAND_HPP
