#include <vix/cli/commands/RegistryCommand.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace vix::commands
{
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

    int run_cmd(const std::string &cmd)
    {
      const int rc = std::system(cmd.c_str());
      return rc == 0 ? 0 : 1;
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
      fs::path dir = registry_dir();
      fs::create_directories(dir.parent_path());

      if (!fs::exists(dir))
      {
        const std::string cmd = "git clone --depth 1 " + registry_repo_url() + " " + dir.string();
        return run_cmd(cmd);
      }

      const std::string cmd = "git -C " + dir.string() + " pull --ff-only";
      return run_cmd(cmd);
    }
  }

  int RegistryCommand::run(const std::vector<std::string> &args)
  {
    if (args.empty())
      return help();

    const std::string sub = args[0];

    if (sub == "sync")
    {
      const int rc = sync_registry();
      if (rc == 0)
        std::cout << "✓ registry synced: " << registry_dir().string() << "\n";
      else
        std::cerr << "✖ registry sync failed\n";
      return rc;
    }

    if (sub == "path")
    {
      std::cout << registry_dir().string() << "\n";
      return 0;
    }

    std::cerr << "vix: unknown registry subcommand '" << sub << "'\n\n";
    return help();
  }

  int RegistryCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix registry <subcommand>\n\n"
        << "Subcommands:\n"
        << "  sync        Clone/pull the registry index into ~/.vix/registry/index\n"
        << "  path        Print local registry path\n\n"
        << "Notes:\n"
        << "  - V1 is Git-based: registry is a repo, not a server.\n";
    return 0;
  }
}
