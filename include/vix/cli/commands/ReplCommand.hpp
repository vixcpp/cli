#ifndef REPL_COMMAND_HPP
#define REPL_COMMAND_HPP

#include <vector>
#include <string>

namespace vix::commands::ReplCommand
{
    int run(const std::vector<std::string> &args);
    int help();
}

#endif
