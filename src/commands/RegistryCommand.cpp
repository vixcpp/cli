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
#include <vix/utils/Env.hpp>
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
      const char *home = vix::utils::vix_getenv("USERPROFILE");
#else
      const char *home = vix::utils::vix_getenv("HOME");
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

    static int git_run(const std::string &cmd)
    {
      return vix::cli::util::run_cmd_retry_debug(cmd);
    }

    static int normalize_registry_worktree(const fs::path &dir)
    {
      // Important:
      // Publish can leave this clone checked out on a temporary publish-* branch.
      // That branch can be deleted after merge, and later pulls will fail.
      // So sync must always re-attach the worktree to origin/main.

      step("fetching origin (prune)...");
      {
        const std::string cmd =
            "git -C " + dir.string() + " fetch -q origin --prune";
        const int rc = git_run(cmd);
        if (rc != 0)
          return rc;
      }

      step("checking out main...");
      {
        // Force checkout even if HEAD is detached or on a deleted publish branch.
        const std::string cmd =
            "git -C " + dir.string() + " checkout -q -B main origin/main";
        const int rc = git_run(cmd);
        if (rc != 0)
          return rc;
      }

      step("resetting to origin/main...");
      {
        // Ensure local main matches origin/main exactly.
        const std::string cmd =
            "git -C " + dir.string() + " reset -q --hard origin/main";
        const int rc = git_run(cmd);
        if (rc != 0)
          return rc;
      }

      return 0;
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

        const int rc = git_run(cmd);
        if (rc != 0)
        {
          vix::cli::util::err_line(std::cerr, "registry sync failed");
          vix::cli::util::warn_line(std::cerr, "Check network + git access, then retry: vix registry sync");
          if (!vix::cli::util::debug_enabled())
            vix::cli::util::warn_line(std::cerr, "Tip: re-run with VIX_DEBUG=1 to see git output");
          return rc;
        }

        // After clone, still normalize to avoid any odd state.
        const int nrc = normalize_registry_worktree(dir);
        if (nrc != 0)
        {
          vix::cli::util::err_line(std::cerr, "registry sync failed");
          vix::cli::util::warn_line(std::cerr, "Check network + git access, then retry: vix registry sync");
          if (!vix::cli::util::debug_enabled())
            vix::cli::util::warn_line(std::cerr, "Tip: re-run with VIX_DEBUG=1 to see git output");
          return nrc;
        }

        vix::cli::util::ok_line(std::cout, "registry synced: " + dir.string());
        return 0;
      }

      // Existing clone: always normalize first, then we are done.
      // Doing pull --ff-only on a deleted publish branch is what caused the failure you saw.
      step("normalizing worktree...");
      const int nrc = normalize_registry_worktree(dir);
      if (nrc != 0)
      {
        vix::cli::util::err_line(std::cerr, "registry sync failed");
        vix::cli::util::warn_line(std::cerr, "Check network + git access, then retry: vix registry sync");
        if (!vix::cli::util::debug_enabled())
          vix::cli::util::warn_line(std::cerr, "Tip: re-run with VIX_DEBUG=1 to see git output");
        return nrc;
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
        << "  sync        Update the local registry index\n"
        << "  path        Print the local registry index path\n\n"

        << "Related commands:\n"
        << "  vix search <query>          Search packages\n"
        << "  vix add <pkg>@<ver>         Add a dependency\n"
        << "  vix remove <pkg>            Remove a dependency\n"
        << "  vix list                    List project dependencies\n"
        << "  vix publish <version>       Publish a package version\n"
        << "  vix store <subcommand>      Manage local dependency store\n"
        << "  vix registry unpublish <namespace/name> [-y|--yes]\n\n"

        << "Examples:\n"
        << "  vix registry sync\n"
        << "  vix registry path\n"
        << "  vix publish 0.2.0\n"
        << "  vix store gc\n";

    return 0;
  }

}
