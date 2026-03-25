/**
 *
 *  @file ListCommand.cpp
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
#include <vix/cli/commands/ListCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  using namespace vix::cli::style;

  namespace
  {
    struct ParsedArgs
    {
      bool globalMode{false};
    };

    fs::path lock_path()
    {
      return fs::current_path() / "vix.lock";
    }

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

    fs::path global_root_dir()
    {
      return vix_root() / "global";
    }

    fs::path global_manifest_path()
    {
      return global_root_dir() / "installed.json";
    }

    json read_json_or_throw(const fs::path &p)
    {
      std::ifstream in(p);
      if (!in)
        throw std::runtime_error("cannot open: " + p.string());

      json j;
      in >> j;
      return j;
    }

    const json *get_project_deps_ptr(const json &lock)
    {
      if (lock.is_array())
        return &lock;

      if (lock.is_object() && lock.contains("dependencies") && lock["dependencies"].is_array())
        return &lock["dependencies"];

      return nullptr;
    }

    const json *get_global_pkgs_ptr(const json &root)
    {
      if (root.is_object() && root.contains("packages") && root["packages"].is_array())
        return &root["packages"];

      return nullptr;
    }

    std::string dep_version(const json &d)
    {
      if (d.contains("version") && d["version"].is_string())
        return d["version"].get<std::string>();

      if (d.contains("tag") && d["tag"].is_string())
      {
        std::string tag = d["tag"].get<std::string>();
        if (!tag.empty() && tag.front() == 'v')
          tag.erase(tag.begin());
        return tag;
      }

      return {};
    }

    ParsedArgs parse_args(const std::vector<std::string> &args)
    {
      ParsedArgs parsed;

      for (const auto &arg : args)
      {
        if (arg == "-g" || arg == "--global")
          parsed.globalMode = true;
      }

      return parsed;
    }

    void print_dep_block(
        const json &d,
        bool globalMode)
    {
      const std::string id = d.value("id", "");
      const std::string ver = dep_version(d);
      const std::string commit = d.value("commit", "");

      std::string repo;
      if (d.contains("repo"))
      {
        if (d["repo"].is_object())
          repo = d["repo"].value("url", "");
        else if (d["repo"].is_string())
          repo = d["repo"].get<std::string>();
      }

      vix::cli::util::dep_line(std::cout, id, ver, commit, repo);

      if (globalMode)
      {
        const std::string type = d.value("type", "");
        const std::string include = d.value("include", "");
        const std::string installedPath = d.value("installed_path", "");

        if (!type.empty())
          vix::cli::util::kv(std::cout, "type", type);

        if (!include.empty())
          vix::cli::util::kv(std::cout, "include", include);

        if (!installedPath.empty())
          vix::cli::util::kv(std::cout, "path", installedPath);
      }

      std::cout << "\n";
    }

  } // namespace

  int ListCommand::run(const std::vector<std::string> &args)
  {
    const ParsedArgs parsed = parse_args(args);

    if (parsed.globalMode)
    {
      vix::cli::util::section(std::cout, "Global packages");

      const fs::path p = global_manifest_path();
      vix::cli::util::kv(std::cout, "manifest", p.string());

      if (!fs::exists(p))
      {
        vix::cli::util::ok_line(std::cout, "No global packages installed.");
        return 0;
      }

      json root;
      try
      {
        root = read_json_or_throw(p);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, std::string("failed to read manifest: ") + ex.what());
        return 1;
      }

      const json *pkgsPtr = get_global_pkgs_ptr(root);
      if (!pkgsPtr)
      {
        vix::cli::util::err_line(std::cerr, "invalid global manifest: expected { packages: [] }");
        return 1;
      }

      const json &pkgs = *pkgsPtr;
      if (pkgs.empty())
      {
        vix::cli::util::ok_line(std::cout, "No global packages installed.");
        return 0;
      }

      std::cout << "\n";

      for (const auto &pkg : pkgs)
        print_dep_block(pkg, true);

      vix::cli::util::ok_line(
          std::cout,
          "Found " + std::to_string(pkgs.size()) + " global package(s).");
      return 0;
    }

    vix::cli::util::section(std::cout, "Project dependencies");

    const fs::path p = lock_path();
    vix::cli::util::kv(std::cout, "lock", p.string());

    if (!fs::exists(p))
    {
      vix::cli::util::err_line(std::cerr, "missing lock file: " + p.string());
      vix::cli::util::warn_line(std::cerr, "Tip: add a package with: vix add <pkg>@<version>");
      return 1;
    }

    json lock;
    try
    {
      lock = read_json_or_throw(p);
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(std::cerr, std::string("failed to read lock: ") + ex.what());
      return 1;
    }

    const json *depsPtr = get_project_deps_ptr(lock);
    if (!depsPtr)
    {
      vix::cli::util::err_line(std::cerr, "invalid lock: expected array or { dependencies: [] }");
      return 1;
    }

    const json &deps = *depsPtr;
    if (deps.empty())
    {
      vix::cli::util::ok_line(std::cout, "No dependencies.");
      return 0;
    }

    std::cout << "\n";

    for (const auto &d : deps)
      print_dep_block(d, false);

    vix::cli::util::ok_line(
        std::cout,
        "Found " + std::to_string(deps.size()) + " dependency(ies).");
    return 0;
  }

  int ListCommand::help()
  {
    std::cout
        << "vix list\n"
        << "List project dependencies or globally installed packages.\n\n"

        << "Usage\n"
        << "  vix list\n"
        << "  vix list -g\n\n"

        << "Examples\n"
        << "  vix list\n"
        << "  vix list -g\n\n"

        << "What happens\n"
        << "  • 'vix list' reads dependencies from vix.lock\n"
        << "  • 'vix list -g' reads packages from ~/.vix/global/installed.json\n";

    return 0;
  }
}
