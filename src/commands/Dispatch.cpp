#include <vix/cli/commands/Dispatch.hpp>

#include <vix/cli/commands/NewCommand.hpp>
#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/commands/DevCommand.hpp>
#include <vix/cli/commands/CheckCommand.hpp>
#include <vix/cli/commands/TestsCommand.hpp>
#include <vix/cli/commands/PackCommand.hpp>
#include <vix/cli/commands/VerifyCommand.hpp>
#include <vix/cli/commands/ReplCommand.hpp>

#include <stdexcept>

namespace vix::cli::dispatch
{
    Dispatcher::Dispatcher()
    {
        auto add = [&](Entry e)
        {
            map_.emplace(e.name, std::move(e));
        };

        // Project
        add({"new",
             "Project",
             "Create a new Vix project",
             [](const Args &a)
             { return vix::commands::NewCommand::run(a); },
             []()
             { return vix::commands::NewCommand::help(); }});

        add({"build",
             "Project",
             "Configure + build project",
             [](const Args &a)
             { return vix::commands::BuildCommand::run(a); },
             []()
             { return vix::commands::BuildCommand::help(); }});

        add({"run",
             "Project",
             "Build (if needed) then run",
             [](const Args &a)
             { return vix::commands::RunCommand::run(a); },
             []()
             { return vix::commands::RunCommand::help(); }});

        add({"dev",
             "Project",
             "Hot reload dev mode",
             [](const Args &a)
             { return vix::commands::DevCommand::run(a); },
             []()
             { return vix::commands::DevCommand::help(); }});

        add({"check",
             "Project",
             "Validate build / script check",
             [](const Args &a)
             { return vix::commands::CheckCommand::run(a); },
             []()
             { return vix::commands::CheckCommand::help(); }});

        add({"tests",
             "Project",
             "Run tests (alias of check --tests)",
             [](const Args &a)
             { return vix::commands::TestsCommand::run(a); },
             []()
             { return vix::commands::TestsCommand::help(); }});

        add({"test",
             "Project",
             "Alias of tests",
             [](const Args &a)
             { return vix::commands::TestsCommand::run(a); },
             []()
             { return vix::commands::TestsCommand::help(); }});

        add({"repl",
             "Project",
             "Start interactive Vix REPL",
             [](const Args &a)
             { return vix::commands::ReplCommand::run(a); },
             []()
             { return vix::commands::ReplCommand::help(); }});

        // Packaging & security
        add({"pack",
             "Packaging",
             "Create dist/<name>@<version> (+ optional .vixpkg)",
             [](const Args &a)
             { return vix::commands::PackCommand::run(a); },
             []()
             { return vix::commands::PackCommand::help(); }});

        add({"verify",
             "Packaging",
             "Verify dist/<name>@<version> or .vixpkg",
             [](const Args &a)
             { return vix::commands::VerifyCommand::run(a); },
             []()
             { return vix::commands::VerifyCommand::help(); }});
    }

    bool Dispatcher::has(const std::string &cmd) const
    {
        return map_.find(cmd) != map_.end();
    }

    int Dispatcher::run(const std::string &cmd, const Args &args) const
    {
        auto it = map_.find(cmd);
        if (it == map_.end())
            return 127;
        return it->second.run(args);
    }

    int Dispatcher::help(const std::string &cmd) const
    {
        auto it = map_.find(cmd);
        if (it == map_.end())
            return 127;
        return it->second.help();
    }

    const std::unordered_map<std::string, Entry> &Dispatcher::entries() const noexcept
    {
        return map_;
    }

    Dispatcher &global()
    {
        static Dispatcher d;
        return d;
    }
}
