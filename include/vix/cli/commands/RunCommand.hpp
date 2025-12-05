#ifndef VIX_RUN_COMMAND_HPP
#define VIX_RUN_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::RunCommand
{
    int run(const std::vector<std::string> &args);
    int help();
}

#endif // VIX_RUN_COMMAND_HPP
