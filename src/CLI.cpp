#include <vix/cli/CLI.hpp>
#include <vix/cli/commands/NewCommand.hpp>
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/utils/Logger.hpp>

#include <iostream>
#include <unordered_map>

namespace Vix
{
    CLI::CLI()
    {
        // Commandes de base
        commands_["help"] = [this](auto args)
        { return help(args); };
        commands_["version"] = [this](auto args)
        { return version(args); };

        // Commandes principales du CLI
        commands_["new"] = [](auto args)
        { return Commands::NewCommand::run(args); };
        commands_["run"] = [](auto args)
        { return Commands::RunCommand::run(args); };
        commands_["build"] = [](auto args)
        { return Commands::BuildCommand::run(args); };

        // Alias pratiques
        commands_["-h"] = [this](auto args)
        { return help(args); };
        commands_["--help"] = [this](auto args)
        { return help(args); };
        commands_["-v"] = [this](auto args)
        { return version(args); };
        commands_["--version"] = [this](auto args)
        { return version(args); };

        // Démo interne (facultative)
        commands_["hello"] = [](auto)
        {
            auto &logger = Logger::getInstance();
            logger.logModule("CLI", Logger::Level::INFO, "Hello from Vix.cpp 👋");
            return 0;
        };
    }

    int CLI::run(int argc, char **argv)
    {
        auto &logger = Logger::getInstance();

        if (argc < 2)
        {
            logger.log(Logger::Level::WARN, "Usage: vix <command> [options]");
            help({});
            return 1;
        }

        std::string cmd = argv[1];
        std::vector<std::string> args(argv + 2, argv + argc);

        if (commands_.count(cmd))
        {
            try
            {
                return commands_[cmd](args);
            }
            catch (const std::exception &ex)
            {
                logger.logModule("CLI", Logger::Level::ERROR, "Command '{}' failed: {}", cmd, ex.what());
                return 1;
            }
        }

        logger.logModule("CLI", Logger::Level::ERROR, "Unknown command: '{}'", cmd);
        help({});
        return 1;
    }

    int CLI::help(const std::vector<std::string> &)
    {
        auto &logger = Logger::getInstance();
        logger.log(Logger::Level::INFO, "");
        logger.log(Logger::Level::INFO, "✨ Vix.cpp CLI - Developer Commands ✨");
        logger.log(Logger::Level::INFO, "----------------------------------------");
        logger.log(Logger::Level::INFO, "  new <name>          Create a new Vix project");
        logger.log(Logger::Level::INFO, "  build [name]        Build a project (root or app)");
        logger.log(Logger::Level::INFO, "  run [name] [--args] Run a project or app");
        logger.log(Logger::Level::INFO, "  version             Show CLI version");
        logger.log(Logger::Level::INFO, "  help                Show this help message");
        logger.log(Logger::Level::INFO, "");
        logger.log(Logger::Level::INFO, "Examples:");
        logger.log(Logger::Level::INFO, "  vix new blog");
        logger.log(Logger::Level::INFO, "  vix build blog --config Debug");
        logger.log(Logger::Level::INFO, "  vix run blog -- --port 8080");
        logger.log(Logger::Level::INFO, "");
        return 0;
    }

    int CLI::version(const std::vector<std::string> &)
    {
        auto &logger = Logger::getInstance();
        logger.log(Logger::Level::INFO, "Vix.cpp CLI version 0.1.0");
        logger.log(Logger::Level::INFO, "Developed by SoftAdAstra");
        return 0;
    }
}
