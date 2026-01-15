#ifndef CHECK_COMMAND_HPP
#define CHECK_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::CheckCommand
{
    int run(const std::vector<std::string> &args);
    int help();
} // namespace vix::commands::CheckCommand

#endif
