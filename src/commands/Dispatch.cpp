/**
 *
 *  @file Dispatch.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/Dispatch.hpp>
#include <vix/cli/commands/NewCommand.hpp>
#include <vix/cli/commands/InitCommand.hpp>
#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/commands/DevCommand.hpp>
#include <vix/cli/commands/TestsCommand.hpp>
#include <vix/cli/commands/ReplCommand.hpp>
#include <vix/cli/commands/AddCommand.hpp>
#include <vix/cli/commands/RemoveCommand.hpp>
#include <vix/cli/commands/PublishCommand.hpp>
#include <vix/cli/commands/InstallCommand.hpp>
#include <vix/cli/commands/UpdateCommand.hpp>

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

    add({"init",
         "Project",
         "Initialize current directory",
         [](const Args &a)
         { return vix::commands::InitCommand::run(a); },
         []()
         { return vix::commands::InitCommand::help(); }});

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
         "Run a C++ project while watching for changes",
         [](const Args &a)
         { return vix::commands::DevCommand::run(a); },
         []()
         { return vix::commands::DevCommand::help(); }});

    add({"test",
         "Project",
         "Run registered project tests",
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

    add({"add",
         "Registry",
         "Add a Registry or Git package dependency",
         [](const Args &a)
         { return vix::commands::AddCommand::run(a); },
         []()
         { return vix::commands::AddCommand::help(); }});

    add({"remove",
         "Registry",
         "Remove a root package dependency",
         [](const Args &a)
         { return vix::commands::RemoveCommand::run(a); },
         []()
         { return vix::commands::RemoveCommand::help(); }});

    add({"update",
         "Registry",
         "Update all dependencies to latest versions",
         [](const Args &a)
         { return vix::commands::UpdateCommand::run(a); },
         []()
         { return vix::commands::UpdateCommand::help(); }});

    add({"publish",
         "Registry",
         "Publish the current package to its configured Registry",
         [](const Args &a)
         { return vix::commands::PublishCommand::run(a); },
         []()
         { return vix::commands::PublishCommand::help(); }});

    add({"install",
         "Registry",
         "Install project dependencies from vix.lock",
         [](const Args &a)
         { return vix::commands::InstallCommand::run(a); },
         []()
         { return vix::commands::InstallCommand::help(); }});

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
