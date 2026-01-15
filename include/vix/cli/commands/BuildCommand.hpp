#ifndef BUILD_COMMAND_HPP
#define BUILD_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::BuildCommand
{
    int run(const std::vector<std::string> &args);
    int help();
}

#endif // BUILD_COMMAND_HPP
