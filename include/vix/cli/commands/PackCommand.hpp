#ifndef PACK_COMMAND_HPP
#define PACK_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::PackCommand
{
    int run(const std::vector<std::string> &args);
    int help();
}

#endif
