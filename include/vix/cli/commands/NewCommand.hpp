// include/vix/cli/commands/NewCommand.hpp
#ifndef VIX_NEW_COMMAND_HPP
#define VIX_NEW_COMMAND_HPP

#include <string>
#include <vector>

namespace Vix::Commands::NewCommand
{
    /**
     * @brief Initialise un nouveau projet Vix.cpp dans le répertoire vix/<project_name>.
     * Usage : vix new myapp  -> crée ./vix/myapp/...
     */
    int run(const std::vector<std::string> &args);
}

#endif // VIX_NEW_COMMAND_HPP
