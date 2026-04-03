/**
 *
 *  @file InfoCommand.cpp
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
#include <vix/cli/commands/InfoCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef VIX_CLI_VERSION
#define VIX_CLI_VERSION "dev"
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  namespace
  {
    static std::string home_dir()
    {
#ifdef _WIN32
      const char *home = vix::utils::vix_getenv("USERPROFILE");
#else
      const char *home = vix::utils::vix_getenv("HOME");
#endif
      return home ? std::string(home) : std::string();
    }

    static fs::path vix_root()
    {
      const std::string h = home_dir();
      if (h.empty())
        return fs::path(".vix");

      return fs::path(h) / ".vix";
    }

    static fs::path registry_index_dir()
    {
      return vix_root() / "registry" / "index";
    }

    static fs::path store_git_dir()
    {
      return vix_root() / "store" / "git";
    }

    static fs::path global_root_dir()
    {
      return vix_root() / "global";
    }

    static fs::path global_manifest_path()
    {
      return global_root_dir() / "installed.json";
    }

    static fs::path artifact_cache_root()
    {
      return vix_root() / "cache" / "build";
    }

    static bool path_exists(const fs::path &p)
    {
      std::error_code ec;
      return fs::exists(p, ec);
    }

    static bool is_directory_path(const fs::path &p)
    {
      std::error_code ec;
      return fs::is_directory(p, ec);
    }

    static bool is_regular_file_path(const fs::path &p)
    {
      std::error_code ec;
      return fs::is_regular_file(p, ec);
    }

    static std::string state_label(const fs::path &p)
    {
      return path_exists(p) ? "present" : "missing";
    }

    static std::string path_with_state(const fs::path &p)
    {
      return p.string() + " [" + state_label(p) + "]";
    }

    static std::string kind_of_path(const fs::path &p)
    {
      if (is_directory_path(p))
        return "directory";

      if (is_regular_file_path(p))
        return "file";

      return "path";
    }

    static std::size_t count_directories(const fs::path &root)
    {
      std::error_code ec;
      if (!fs::exists(root, ec) || !fs::is_directory(root, ec))
        return 0;

      std::size_t count = 0;

      for (const auto &entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec))
      {
        if (ec)
          break;

        std::error_code ec2;
        if (entry.is_directory(ec2) && !ec2)
          ++count;
      }

      return count;
    }

    static std::size_t count_store_commits(const fs::path &storeRoot)
    {
      std::error_code ec;
      if (!fs::exists(storeRoot, ec) || !fs::is_directory(storeRoot, ec))
        return 0;

      std::size_t count = 0;

      for (const auto &pkgDir : fs::directory_iterator(storeRoot, fs::directory_options::skip_permission_denied, ec))
      {
        if (ec)
          break;

        std::error_code ecPkg;
        if (!pkgDir.is_directory(ecPkg) || ecPkg)
          continue;

        for (const auto &commitDir : fs::directory_iterator(pkgDir.path(), fs::directory_options::skip_permission_denied, ec))
        {
          if (ec)
            break;

          std::error_code ecCommit;
          if (commitDir.is_directory(ecCommit) && !ecCommit)
            ++count;
        }
      }

      return count;
    }

    static std::uintmax_t dir_size_bytes(const fs::path &p)
    {
      std::uintmax_t total = 0;
      std::error_code ec;

      if (!fs::exists(p, ec))
        return 0;

      for (auto it = fs::recursive_directory_iterator(
               p,
               fs::directory_options::skip_permission_denied,
               ec);
           it != fs::recursive_directory_iterator();
           it.increment(ec))
      {
        if (ec)
          continue;

        std::error_code ec2;
        if (it->is_regular_file(ec2) && !ec2)
        {
          const auto sz = it->file_size(ec2);
          if (!ec2)
            total += sz;
        }
      }

      return total;
    }

    static std::string human_bytes(std::uintmax_t bytes)
    {
      const char *units[] = {"B", "KB", "MB", "GB", "TB"};
      double value = static_cast<double>(bytes);
      int unit = 0;

      while (value >= 1024.0 && unit < 4)
      {
        value /= 1024.0;
        ++unit;
      }

      char buf[64];
      if (unit == 0)
      {
        std::snprintf(
            buf,
            sizeof(buf),
            "%llu %s",
            static_cast<unsigned long long>(bytes),
            units[unit]);
      }
      else
      {
        std::snprintf(buf, sizeof(buf), "%.2f %s", value, units[unit]);
      }

      return std::string(buf);
    }

    static std::size_t count_global_packages_from_manifest()
    {
      const fs::path manifest = global_manifest_path();
      if (!is_regular_file_path(manifest))
        return 0;

      try
      {
        std::ifstream in(manifest);
        if (!in)
          return 0;

        json root;
        in >> root;

        if (!root.is_object())
          return 0;

        if (!root.contains("packages") || !root["packages"].is_array())
          return 0;

        return root["packages"].size();
      }
      catch (...)
      {
        return 0;
      }
    }

    static std::string yes_no(bool value)
    {
      return value ? "yes" : "no";
    }

    static void print_status_line(const std::string &label, const fs::path &path)
    {
      const std::string state = state_label(path);
      const std::string type = kind_of_path(path);

      vix::cli::util::kv(
          std::cout,
          label,
          state + " (" + type + ")");
    }

    static void print_path_line(const std::string &label, const fs::path &path)
    {
      vix::cli::util::kv(
          std::cout,
          label,
          path_with_state(path));
    }
  }

  int InfoCommand::run(const std::vector<std::string> &args)
  {
    if (!args.empty())
      return help();

    const fs::path root = vix_root();
    const fs::path registry = registry_index_dir();
    const fs::path store = store_git_dir();
    const fs::path globalRoot = global_root_dir();
    const fs::path globalManifest = global_manifest_path();
    const fs::path artifacts = artifact_cache_root();

    const bool rootExists = path_exists(root);
    const bool registryExists = path_exists(registry);
    const bool storeExists = path_exists(store);
    const bool globalRootExists = path_exists(globalRoot);
    const bool globalManifestExists = path_exists(globalManifest);
    const bool artifactsExists = path_exists(artifacts);

    const std::size_t globalPackageCount = count_global_packages_from_manifest();
    const std::size_t storePackageDirs = count_directories(store);
    const std::size_t storeCommitCount = count_store_commits(store);

    const std::uintmax_t storeBytes = dir_size_bytes(store);
    const std::uintmax_t artifactBytes = dir_size_bytes(artifacts);

    vix::cli::util::section(std::cout, "Info");
    vix::cli::util::kv(std::cout, "version", VIX_CLI_VERSION);
    vix::cli::util::kv(std::cout, "home", home_dir().empty() ? "(not set)" : home_dir());
    vix::cli::util::kv(std::cout, "root", root.string());
    vix::cli::util::kv(std::cout, "root state", rootExists ? "present" : "missing");

    vix::cli::util::one_line_spacer(std::cout);

    vix::cli::util::section(std::cout, "Environment");
    print_path_line("vix root", root);
    print_path_line("registry", registry);
    print_path_line("store", store);
    print_path_line("global root", globalRoot);
    print_path_line("global manifest", globalManifest);

    vix::cli::util::one_line_spacer(std::cout);

    vix::cli::util::section(std::cout, "Caches");
    print_path_line("artifact cache", artifacts);
    vix::cli::util::kv(std::cout, "store packages", std::to_string(storePackageDirs));
    vix::cli::util::kv(std::cout, "store commits", std::to_string(storeCommitCount));
    vix::cli::util::kv(std::cout, "global packages", std::to_string(globalPackageCount));
    vix::cli::util::kv(std::cout, "store size", human_bytes(storeBytes));
    vix::cli::util::kv(std::cout, "artifact size", human_bytes(artifactBytes));

    vix::cli::util::one_line_spacer(std::cout);

    vix::cli::util::section(std::cout, "Status");
    print_status_line("registry index", registry);
    print_status_line("store cache", store);
    print_status_line("global root", globalRoot);
    print_status_line("global manifest", globalManifest);
    print_status_line("artifact cache", artifacts);
    vix::cli::util::kv(std::cout, "globals usable", yes_no(globalRootExists && globalManifestExists));
    vix::cli::util::kv(std::cout, "registry usable", yes_no(registryExists));
    vix::cli::util::kv(std::cout, "store usable", yes_no(storeExists));
    vix::cli::util::kv(std::cout, "artifacts usable", yes_no(artifactsExists));

    vix::cli::util::one_line_spacer(std::cout);

    if (rootExists)
      vix::cli::util::ok_line(std::cout, "Vix environment detected");
    else
      vix::cli::util::warn_line(std::cout, "Vix root is missing. Some commands may create it on demand.");

    return 0;
  }

  int InfoCommand::help()
  {
    std::cout
        << "vix info\n"
        << "Show Vix environment, paths, caches, and local state.\n\n"

        << "Usage\n"
        << "  vix info\n\n"

        << "What it shows\n"
        << "  • Vix version\n"
        << "  • Vix root path\n"
        << "  • Registry index path and state\n"
        << "  • Store cache path and state\n"
        << "  • Global packages manifest path and state\n"
        << "  • Build artifact cache path and state\n"
        << "  • Number of global packages\n"
        << "  • Number of package directories in store/git\n"
        << "  • Number of cached commits in store/git\n"
        << "  • Disk usage of store/git\n"
        << "  • Disk usage of build artifact cache\n";

    return 0;
  }
}
