#include "vix/cli/commands/RunCommand.hpp"
#include "vix/core/utils/Logger.hpp"

namespace Vix::Commands::RunCommand
{
    int run(const std::vector<std::string> &args)
    {
        auto &logger = Vix::Logger::getInstance();
        logger.logModule("RunCommand", Vix::Logger::Level::INFO, "Running project (stub)...");
        return 0;
    }
}
