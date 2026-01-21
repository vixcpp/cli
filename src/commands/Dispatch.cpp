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
#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/commands/DevCommand.hpp>
#include <vix/cli/commands/CheckCommand.hpp>
#include <vix/cli/commands/TestsCommand.hpp>
#include <vix/cli/commands/PackCommand.hpp>
#include <vix/cli/commands/VerifyCommand.hpp>
#include <vix/cli/commands/ReplCommand.hpp>
#include <vix/cli/commands/InstallCommand.hpp>
#include <vix/cli/commands/OrmCommand.hpp>
#include <vix/cli/commands/RegistryCommand.hpp>
#include <vix/cli/commands/AddCommand.hpp>
#include <vix/cli/commands/SearchCommand.hpp>
#include <vix/cli/commands/RemoveCommand.hpp>
#include <vix/cli/commands/ListCommand.hpp>
#include <vix/cli/commands/StoreCommand.hpp>
#include <vix/cli/commands/PublishCommand.hpp>
#include <vix/cli/commands/DepsCommand.hpp>
#include <vix/cli/commands/ModulesCommand.hpp>

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

    add({"orm",
         "Database",
         "Migrations/status/rollback",
         [](const Args &a)
         { return vix::commands::OrmCommand::run(a); },
         []()
         { return vix::commands::OrmCommand::help(); }});

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
    add({"install",
         "Packaging",
         "Install a package into the local store",
         [](const Args &a)
         { return vix::commands::InstallCommand::run(a); },
         []()
         { return vix::commands::InstallCommand::help(); }});

    // Registry
    add({"registry",
         "Registry",
         "Sync/search the Git-based registry index",
         [](const Args &a)
         { return vix::commands::RegistryCommand::run(a); },
         []()
         { return vix::commands::RegistryCommand::help(); }});

    add({"add",
         "Registry",
         "Add a dependency from registry (pins commit)",
         [](const Args &a)
         { return vix::commands::AddCommand::run(a); },
         []()
         { return vix::commands::AddCommand::help(); }});

    add({"search",
         "Registry",
         "Search packages in local registry index (offline)",
         [](const Args &a)
         { return vix::commands::SearchCommand::run(a); },
         []()
         { return vix::commands::SearchCommand::help(); }});

    add({"remove",
         "Registry",
         "Remove a dependency from vix.lock",
         [](const Args &a)
         { return vix::commands::RemoveCommand::run(a); },
         []()
         { return vix::commands::RemoveCommand::help(); }});

    add({"list",
         "Registry",
         "List project dependencies from vix.lock",
         [](const Args &a)
         { return vix::commands::ListCommand::run(a); },
         []()
         { return vix::commands::ListCommand::help(); }});

    add({"store",
         "Registry",
         "Manage local store cache (gc/path)",
         [](const Args &a)
         { return vix::commands::StoreCommand::run(a); },
         []()
         { return vix::commands::StoreCommand::help(); }});

    add({"publish",
         "Registry",
         "Publish a package version to the registry (JSON + PR)",
         [](const Args &a)
         { return vix::commands::PublishCommand::run(a); },
         []()
         { return vix::commands::PublishCommand::help(); }});

    add({"deps",
         "Registry",
         "Install deps from vix.lock into .vix/ and generate CMake file",
         [](const Args &a)
         { return vix::commands::DepsCommand::run(a); },
         []()
         { return vix::commands::DepsCommand::help(); }});

    add({"modules",
         "Project",
         "Optional project modules (init/add/list/check)",
         [](const Args &a)
         { return vix::commands::ModulesCommand::run(a); },
         []()
         { return vix::commands::ModulesCommand::help(); }});
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
