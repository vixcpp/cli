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

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

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

    fs::path manifest_path()
    {
      return fs::current_path() / "vix.json";
    }

    std::string registry_repo_url()
    {
      return "https://github.com/vixcpp/registry.git";
    }

    static int git_run(const std::string &cmd)
    {
      return vix::cli::util::run_cmd_retry_debug(cmd);
    }

    static std::string default_project_name()
    {
      std::string name = fs::current_path().filename().string();
      if (name.empty())
        name = "my-lib";
      return name;
    }

    static int normalize_registry_worktree(const fs::path &dir)
    {
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
        const std::string cmd =
            "git -C " + dir.string() + " checkout -q -B main origin/main";
        const int rc = git_run(cmd);
        if (rc != 0)
          return rc;
      }

      step("resetting to origin/main...");
      {
        const std::string cmd =
            "git -C " + dir.string() + " reset -q --hard origin/main";
        const int rc = git_run(cmd);
        if (rc != 0)
          return rc;
      }

      return 0;
    }

    static int init_registry_manifest(bool force)
    {
      const fs::path path = manifest_path();

      vix::cli::util::section(std::cout, "Registry");
      vix::cli::util::kv(std::cout, "path", path.string());

      if (fs::exists(path) && !force)
      {
        vix::cli::util::err_line(std::cerr, "vix.json already exists");
        vix::cli::util::warn_line(std::cerr, "Use: vix registry init --force");
        return 1;
      }

      const std::string projectName = default_project_name();

      json manifest = json::object();
      manifest["name"] = projectName;
      manifest["namespace"] = "your-namespace";
      manifest["version"] = "0.1.0";
      manifest["type"] = "header-only";
      manifest["include"] = "include";
      manifest["deps"] = json::array();
      manifest["license"] = "MIT";
      manifest["description"] = "A tiny header-only C++ library.";
      manifest["keywords"] = json::array({"cpp", "header-only", "vix"});
      manifest["repository"] = "https://github.com/your-username/" + projectName;
      manifest["authors"] = json::array({
          json::object({
              {"name", "Your Name"},
              {"github", "your-username"},
          }),
      });

      std::ofstream out(path);
      if (!out)
      {
        vix::cli::util::err_line(std::cerr, "failed to create vix.json");
        return 1;
      }

      out << manifest.dump(2) << "\n";
      out.close();

      vix::cli::util::ok_line(std::cout, "created: " + path.string());
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
  } // namespace

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

    if (sub == "init")
    {
      bool force = false;

      for (std::size_t i = 1; i < args.size(); ++i)
      {
        const std::string &arg = args[i];

        if (arg == "--force")
        {
          force = true;
          continue;
        }

        vix::cli::util::err_line(std::cerr, "unknown flag: " + arg);
        vix::cli::util::warn_line(std::cerr, "Try: vix registry init");
        vix::cli::util::warn_line(std::cerr, "Try: vix registry init --force");
        return 1;
      }

      return init_registry_manifest(force);
    }

    vix::cli::util::err_line(std::cerr, "unknown registry subcommand: " + sub);
    vix::cli::util::warn_line(std::cerr, "Try: vix registry init");
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
        << "  init        Create a local vix.json manifest for a package\n"
        << "  sync        Update the local registry index\n"
        << "  path        Print the local registry index path\n\n"

        << "Init options:\n"
        << "  --force     Overwrite existing vix.json\n\n"

        << "Related commands:\n"
        << "  vix search <query>          Search packages\n"
        << "  vix add <pkg>@<ver>         Add a dependency\n"
        << "  vix remove <pkg>            Remove a dependency\n"
        << "  vix list                    List project dependencies\n"
        << "  vix publish <version>       Publish a package version\n"
        << "  vix store <subcommand>      Manage local dependency store\n"
        << "  vix registry unpublish <namespace/name> [-y|--yes]\n\n"

        << "Examples:\n"
        << "  vix registry init\n"
        << "  vix registry init --force\n"
        << "  vix registry sync\n"
        << "  vix registry path\n"
        << "  vix publish 0.2.0\n"
        << "  vix store gc\n";

    return 0;
  }

} // namespace vix::commands
