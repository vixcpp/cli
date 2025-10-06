#include "vix/cli/CLI.hpp"

namespace Vix
{
    CLI::CLI()
    {
        commands_["hello"] = [this](auto args)
        { return help(args); };
        commands_["version"] = [this](auto args)
        {
            return version(args);
        };
        // new , run, build, serve
    }

    int CLI::run(int argc, char **argv)
    {
        if (argc < 2)
        {
            std::cout << "Usage: vix <command> [options]\n";
            return 1;
        }

        std::string cmd = argv[1];
        std::vector<std::string> args(argv + 2, argv + argc);

        if (commands_.count(cmd))
        {
            return commands_[cmd](args);
        }
        else
        {
            std::cout << "Unknow command: " << cmd << "\n";
            help({});
            return 1;
        }
    }

    int CLI::help(const std::vector<std::string> &)
    {
        std::cout << "Vix.cpp CLI - Available commandes:\n"
                  << " new <name> Create a new Vix project\n"
                  << " run Run the server\n"
                  << " build Build the project\n"
                  << " version Show version\n"
                  << " help Show this help message\n";
        return 0;
    }

    int CLI::version(const std::vector<std::string> &)
    {
        std::cout << "Vix.cpp CLI version 0.1.0\n";
        return 0;
    }
}