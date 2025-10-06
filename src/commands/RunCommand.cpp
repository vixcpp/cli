#include "vix/cli/commands/RunCommand.hpp"
#include <iostream>

namespace Vix::Commands::RunCommand
{
    int run(const std::vector<std::string> &args)
    {
        std::cout << "Running project (stub)...\n";
        return 0;
    }
}