#ifndef RUN_COMMAND_HPP
#define RUN_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::RunCommand
{
    int run(const std::vector<std::string> &args);
    int help();
}

#endif // RUN_COMMAND_HPP
