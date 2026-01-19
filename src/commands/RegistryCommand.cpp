/**
 *
 *  @file RegistryCommand.cpp
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
#include <vix/cli/commands/RegistryCommand.hpp>
#include <vix/cli/util/Shell.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/Style.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace vix::commands
{
  using namespace vix::cli::style;

  namespace
  {
    std::string home_dir()
    {
#ifdef _WIN32
      const char *home = std::getenv("USERPROFILE");
#else
      const char *home = std::getenv("HOME");
#endif
      return home ? std::string(home) : std::string();
    }

    fs::path vix_root()
    {
      const std::string h = home_dir();
      if (h.empty())
        return fs::path(".vix");
      return fs::path(h) / ".vix";
    }

    fs::path registry_dir()
    {
      return vix_root() / "registry" / "index";
    }

    std::string registry_repo_url()
    {
      return "https://github.com/vixcpp/registry.git";
    }

    int sync_registry()
    {
      const fs::path dir = registry_dir();
      fs::create_directories(dir.parent_path());

      vix::cli::util::section(std::cout, "Registry");
      vix::cli::util::kv(std::cout, "path", dir.string());

      if (!fs::exists(dir))
      {
        step("cloning index (depth=1)...");
        const std::string cmd =
            "git clone -q --depth 1 " + registry_repo_url() + " " + dir.string();

        const int rc = vix::cli::util::run_cmd_retry_debug(cmd);
        if (rc != 0)
        {
          vix::cli::util::err_line(std::cerr, "registry sync failed");
          vix::cli::util::warn_line(std::cerr, "Check network + git access, then retry: vix registry sync");
          if (!vix::cli::util::debug_enabled())
            vix::cli::util::warn_line(std::cerr, "Tip: re-run with VIX_DEBUG=1 to see git output");
          return rc;
        }

        vix::cli::util::ok_line(std::cout, "registry synced: " + dir.string());
        return 0;
      }

      step("updating index (ff-only)...");
      const std::string cmd =
          "git -C " + dir.string() + " pull -q --ff-only";

      const int rc = vix::cli::util::run_cmd_retry_debug(cmd);
      if (rc != 0)
      {
        vix::cli::util::err_line(std::cerr, "registry sync failed");
        vix::cli::util::warn_line(std::cerr, "Check network + git access, then retry: vix registry sync");
        if (!vix::cli::util::debug_enabled())
          vix::cli::util::warn_line(std::cerr, "Tip: re-run with VIX_DEBUG=1 to see git output");
        return rc;
      }

      vix::cli::util::ok_line(std::cout, "registry synced: " + dir.string());
      return 0;
    }
  }

  int RegistryCommand::run(const std::vector<std::string> &args)
  {
    if (args.empty())
      return help();

    const std::string sub = args[0];

    if (sub == "sync")
      return sync_registry();

    if (sub == "path")
    {
      vix::cli::util::section(std::cout, "Registry");
      vix::cli::util::kv(std::cout, "path", registry_dir().string());
      return 0;
    }

    vix::cli::util::err_line(std::cerr, "unknown registry subcommand: " + sub);
    vix::cli::util::warn_line(std::cerr, "Try: vix registry sync");
    vix::cli::util::warn_line(std::cerr, "Try: vix registry path");
    return help();
  }

  int RegistryCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix registry <subcommand>\n\n"

        << "Subcommands:\n"
        << "  sync        Clone or update the registry index (git-based)\n"
        << "  path        Print local registry index path\n\n"

        << "Description:\n"
        << "  The Vix registry is a Git repository containing package metadata.\n"
        << "  It maps <namespace>/<name>@<version> to immutable git commits.\n\n"

        << "Notes:\n"
        << "  - Registry is Git-based (V1): no server, no API, no auth.\n"
        << "  - All searches are local after 'vix registry sync'.\n"
        << "  - Packages are pinned to commits for reproducible builds.\n\n"

        << "Related commands:\n"
        << "  vix search <query>      Search packages in the local registry index\n"
        << "  vix add <pkg>@<ver>     Add a dependency from the registry\n"
        << "  vix list                List project dependencies (from vix.lock)\n"
        << "  vix remove <pkg>        Remove a dependency from the project\n\n"

        << "Examples:\n"
        << "  vix registry sync\n"
        << "  vix registry path\n"
        << "  vix search tree\n"
        << "  vix add gaspardkirira/tree@0.1.0\n";

    return 0;
  }

}
