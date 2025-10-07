#include "vix/cli/commands/BuildCommand.hpp"
#include <vix/utils/Logger.hpp>

namespace Vix::Commands::BuildCommand
{
    int run(const std::vector<std::string> &args)
    {
        auto &logger = Vix::Logger::getInstance();
        logger.logModule("BuildCommand", Vix::Logger::Level::INFO, "Building project (stub)...");
        return 0;
    }
}
