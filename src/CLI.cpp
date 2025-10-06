#include "vix/cli/CLI.hpp"
#include "vix/cli/commands/NewCommand.hpp"
#include "vix/cli/commands/RunCommand.hpp"
#include "vix/cli/commands/BuildCommand.hpp"

namespace Vix
{
    CLI::CLI()
    {
        commands_["hello"] = [this](auto args)
        { return help(args); };
        commands_["version"] = [this](auto args)
        { return version(args); };
        commands_["help"] = [this](auto args)
        { return help(args); };

        commands_["new"] = [](auto args)
        { return Commands::NewCommand::run(args); };
        commands_["run"] = [](auto args)
        { return Commands::RunCommand::run(args); };
        commands_["build"] = [](auto args)
        { return Commands::BuildCommand::run(args); };
    }

    int CLI::run(int argc, char **argv)
    {
        if (argc < 2)
        {
            std::cout << "Usage: vix <command> [options]\n";
            help({});
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
            std::cout << "Unknown command: " << cmd << "\n";
            help({});
            return 1;
        }
    }

    int CLI::help(const std::vector<std::string> &)
    {
        std::cout << "Vix.cpp CLI - Available commands:\n"
                  << "  new <name>    Create a new Vix project\n"
                  << "  run           Run the server\n"
                  << "  build         Build the project\n"
                  << "  version       Show version\n"
                  << "  help          Show this help message\n";
        return 0;
    }

    int CLI::version(const std::vector<std::string> &)
    {
        std::cout << "Vix.cpp CLI version 0.1.0\n";
        return 0;
    }
}
