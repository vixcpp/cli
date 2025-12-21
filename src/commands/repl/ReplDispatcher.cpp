#include <vix/cli/commands/repl/ReplDispatcher.hpp>

#include <vix/cli/commands/NewCommand.hpp>
#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/commands/TestsCommand.hpp>
#include <vix/cli/commands/CheckCommand.hpp>
#include <vix/cli/commands/DevCommand.hpp>
#include <vix/cli/commands/PackCommand.hpp>
#include <vix/cli/commands/VerifyCommand.hpp>

#include <stdexcept>

namespace vix::cli::repl
{
    Dispatcher::Dispatcher()
    {
        // One single map → REPL & future CLI can reuse this file if you want
        map_["new"] = [](auto args)
        { return vix::commands::NewCommand::run(args); };
        map_["build"] = [](auto args)
        { return vix::commands::BuildCommand::run(args); };
        map_["run"] = [](auto args)
        { return vix::commands::RunCommand::run(args); };
        map_["dev"] = [](auto args)
        { return vix::commands::DevCommand::run(args); };
        map_["check"] = [](auto args)
        { return vix::commands::CheckCommand::run(args); };
        map_["tests"] = [](auto args)
        { return vix::commands::TestsCommand::run(args); };
        map_["test"] = [](auto args)
        { return vix::commands::TestsCommand::run(args); };
        map_["pack"] = [](auto args)
        { return vix::commands::PackCommand::run(args); };
        map_["verify"] = [](auto args)
        { return vix::commands::VerifyCommand::run(args); };
    }

    bool Dispatcher::has(const std::string &cmd) const
    {
        return map_.find(cmd) != map_.end();
    }

    int Dispatcher::dispatch(const std::string &cmd, const std::vector<std::string> &args) const
    {
        auto it = map_.find(cmd);
        if (it == map_.end())
            return 127;
        return it->second(args);
    }
}
