#include "vix/cli/commands/NewCommand.hpp"
#include <filesystem>
#include <iostream>

namespace Vix::Commands::NewCommand
{
    int run(const std::vector<std::string> &args)
    {
        if (args.empty())
        {
            std::cerr << "Usage: vix new <project_name>\n";
            return 1;
        }

        const std::string &name = args[0];
        std::filesystem::create_directories(name + "/src");
        std::filesystem::create_directories(name + "/include");

        std::cout << "Project '" << name << "' created!\n";
        return 0;
    }
}