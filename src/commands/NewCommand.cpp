#include "vix/cli/commands/NewCommand.hpp"
#include <vix/utils/Logger.hpp>
#include <filesystem>

namespace Vix::Commands::NewCommand
{
    int run(const std::vector<std::string> &args)
    {
        auto &logger = Vix::Logger::getInstance();

        if (args.empty())
        {
            logger.logModule("NewCommand", Vix::Logger::Level::ERROR, "Usage: vix new <project_name>");
            return 1;
        }

        const std::string &name = args[0];
        std::filesystem::create_directories(name + "/src");
        std::filesystem::create_directories(name + "/include");

        logger.logModule("NewCommand", Vix::Logger::Level::INFO, "Project '{}' created!", name);
        return 0;
    }
}
