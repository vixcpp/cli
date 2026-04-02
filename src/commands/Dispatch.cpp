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
#include <vix/cli/commands/CacheCommand.hpp>
#include <vix/cli/commands/OrmCommand.hpp>
#include <vix/cli/commands/RegistryCommand.hpp>
#include <vix/cli/commands/AddCommand.hpp>
#include <vix/cli/commands/SearchCommand.hpp>
#include <vix/cli/commands/RemoveCommand.hpp>
#include <vix/cli/commands/ListCommand.hpp>
#include <vix/cli/commands/StoreCommand.hpp>
#include <vix/cli/commands/PublishCommand.hpp>
#include <vix/cli/commands/InstallCommand.hpp>
#include <vix/cli/commands/ModulesCommand.hpp>
#include <vix/cli/commands/P2PCommand.hpp>
#include <vix/cli/commands/UpgradeCommand.hpp>
#include <vix/cli/commands/DoctorCommand.hpp>
#include <vix/cli/commands/UninstallCommand.hpp>
#include <vix/cli/commands/UnpublishCommand.hpp>
#include <vix/cli/commands/UpdateCommand.hpp>
#include <vix/cli/commands/OutdatedCommand.hpp>
#include <vix/cli/commands/MakeCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/commands/CompletionCommand.hpp>

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

    add({"completion",
         "Info",
         "Generate shell completion script",
         [](const Args &a)
         { return vix::commands::CompletionCommand::run(a); },
         []()
         { return vix::commands::CompletionCommand::help(); }});

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

    add({"p2p",
         "Network",
         "Run P2P node (tcp + discovery + bootstrap)",
         [](const Args &a)
         { return vix::commands::P2PCommand::run(a); },
         []()
         { return vix::commands::P2PCommand::help(); }});

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
    add({"cache",
         "Packaging",
         "Cache a package into the local store",
         [](const Args &a)
         { return vix::commands::CacheCommand::run(a); },
         []()
         { return vix::commands::CacheCommand::help(); }});

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

    add({"update",
         "Registry",
         "Update all dependencies to latest versions",
         [](const Args &a)
         { return vix::commands::UpdateCommand::run(a); },
         []()
         { return vix::commands::UpdateCommand::help(); }});

    add({"up",
         "Registry",
         "Alias for update",
         [](const Args &a)
         { return vix::commands::UpdateCommand::run(a); },
         []()
         { return vix::commands::UpdateCommand::help(); }});

    add({"outdated",
         "Registry",
         "Check which dependencies are behind the latest versions",
         [](const Args &a)
         { return vix::commands::OutdatedCommand::run(a); },
         []()
         { return vix::commands::OutdatedCommand::help(); }});

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

    add({"unpublish",
         "Registry",
         "Remove a package from the registry (opens PR, destructive)",
         [](const Args &a)
         {
           return vix::commands::UnpublishCommand{}.run(a);
         },
         []()
         {
           return vix::commands::UnpublishCommand{}.help();
         }});

    add({"install",
         "Registry",
         "Install project dependencies from vix.lock",
         [](const Args &a)
         { return vix::commands::InstallCommand::run(a); },
         []()
         { return vix::commands::InstallCommand::help(); }});

    add({"i",
         "Registry",
         "Alias for install",
         [](const Args &a)
         { return vix::commands::InstallCommand::run(a); },
         []()
         { return vix::commands::InstallCommand::help(); }});

    add({"deps",
         "Registry",
         "Alias for install (deprecated)",
         [](const Args &a)
         {
           vix::cli::util::warn_line(std::cout, "'vix deps' is deprecated, use 'vix install'");
           return vix::commands::InstallCommand::run(a);
         },
         []()
         {
           vix::cli::util::warn_line(std::cout, "'vix deps' is deprecated, use 'vix install'");
           return 0;
         }});

    add({"upgrade",
         "Info",
         "Upgrade the Vix CLI binary",
         [](const Args &a)
         { return vix::commands::UpgradeCommand::run(a); },
         []()
         { return vix::commands::UpgradeCommand::help(); }});

    add({"doctor",
         "Info",
         "Check toolchain and install health",
         [](const Args &a)
         { return vix::commands::DoctorCommand::run(a); },
         []()
         { return vix::commands::DoctorCommand::help(); }});

    add({"uninstall",
         "Info",
         "Remove Vix CLI from the system",
         [](const Args &a)
         { return vix::commands::UninstallCommand::run(a); },
         []()
         { return vix::commands::UninstallCommand::help(); }});

    add({"modules",
         "Project",
         "Optional project modules (init/add/list/check)",
         [](const Args &a)
         { return vix::commands::ModulesCommand::run(a); },
         []()
         { return vix::commands::ModulesCommand::help(); }});

    add({"make",
         "Project",
         "Generate C++ code scaffolding (class, struct, enum, function, ...)",
         [](const Args &a)
         { return vix::commands::MakeCommand::run(a); },
         []()
         { return vix::commands::MakeCommand::help(); }});
  }

  bool Dispatcher::has(const std::string &cmd) const
  {
    if (cmd.size() > 5 && cmd.rfind("make:", 0) == 0)
      return map_.find("make") != map_.end();

    return map_.find(cmd) != map_.end();
  }

  int Dispatcher::run(const std::string &cmd, const Args &args) const
  {
    if (cmd.size() > 5 && cmd.rfind("make:", 0) == 0)
    {
      auto it = map_.find("make");
      if (it == map_.end())
        return 127;

      Args forwarded_args;
      forwarded_args.reserve(args.size() + 1);
      forwarded_args.push_back(cmd.substr(5));

      for (const auto &arg : args)
        forwarded_args.push_back(arg);

      return it->second.run(forwarded_args);
    }

    auto it = map_.find(cmd);
    if (it == map_.end())
      return 127;

    return it->second.run(args);
  }

  int Dispatcher::help(const std::string &cmd) const
  {
    if (cmd.size() > 5 && cmd.rfind("make:", 0) == 0)
    {
      auto it = map_.find("make");
      if (it == map_.end())
        return 127;

      return it->second.help();
    }

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
