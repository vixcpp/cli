#ifndef VIX_BUILD_COMMAND_HPP
#define VIX_BUILD_COMMAND_HPP

#include <string>
#include <vector>

namespace Vix::Commands::BuildCommand
{
    /**
     * @brief Run the "build" command.
     * @param args CLI arguments after "build".
     * @return 0 on success, non-zero on failure.
     */
    int run(const std::vector<std::string> &args);
}

#endif // VIX_BUILD_COMMAND_HPP
