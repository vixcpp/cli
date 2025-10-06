#include "vix/cli/CLI.hpp"
#include "vix/cli/commands/NewCommand.hpp"
#include "vix/cli/commands/RunCommand.hpp"
#include "vix/cli/commands/BuildCommand.hpp"
#include "vix/core/utils/Logger.hpp"

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
        auto &logger = Logger::getInstance();

        if (argc < 2)
        {
            logger.log(Logger::Level::INFO, "Usage: vix <command> [options]");
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
            logger.logModule("CLI", Logger::Level::WARN, "Unknown command: {}", cmd);
            help({});
            return 1;
        }
    }

    int CLI::help(const std::vector<std::string> &)
    {
        auto &logger = Logger::getInstance();
        logger.log(Logger::Level::INFO, "Vix.cpp CLI - Available commands:");
        logger.log(Logger::Level::INFO, "  new <name>    Create a new Vix project");
        logger.log(Logger::Level::INFO, "  run           Run the server");
        logger.log(Logger::Level::INFO, "  build         Build the project");
        logger.log(Logger::Level::INFO, "  version       Show version");
        logger.log(Logger::Level::INFO, "  help          Show this help message");
        return 0;
    }

    int CLI::version(const std::vector<std::string> &)
    {
        auto &logger = Logger::getInstance();
        logger.log(Logger::Level::INFO, "Vix.cpp CLI version 0.1.0");
        return 0;
    }
}
