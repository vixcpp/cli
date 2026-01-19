/**
 *
 *  @file StoreCommand.cpp
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
#include <vix/cli/commands/StoreCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/Style.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

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

    fs::path store_root_git()
    {
      return vix_root() / "store" / "git";
    }

    fs::path lock_path()
    {
      return fs::current_path() / "vix.lock";
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

    const json *get_deps_ptr(const json &lock)
    {
      if (lock.is_array())
        return &lock;
      if (lock.is_object() && lock.contains("dependencies") && lock["dependencies"].is_array())
        return &lock["dependencies"];
      return nullptr;
    }

    std::string pkg_dir_from_id(const std::string &id)
    {
      // "namespace/name" -> "namespace.name"
      const auto pos = id.find('/');
      if (pos == std::string::npos)
        return {};
      const std::string ns = id.substr(0, pos);
      const std::string name = id.substr(pos + 1);
      if (ns.empty() || name.empty())
        return {};
      return ns + "." + name;
    }

    std::uintmax_t dir_size_bytes(const fs::path &p)
    {
      std::uintmax_t total = 0;
      std::error_code ec;

      if (!fs::exists(p, ec))
        return 0;

      for (auto it = fs::recursive_directory_iterator(
               p, fs::directory_options::skip_permission_denied, ec);
           it != fs::recursive_directory_iterator(); it.increment(ec))
      {
        if (ec)
          continue;

        std::error_code ec2;
        if (it->is_regular_file(ec2))
        {
          const auto sz = it->file_size(ec2);
          if (!ec2)
            total += sz;
        }
      }

      return total;
    }

    std::string human_bytes(std::uintmax_t bytes)
    {
      const char *units[] = {"B", "KB", "MB", "GB", "TB"};
      double v = static_cast<double>(bytes);
      int u = 0;
      while (v >= 1024.0 && u < 4)
      {
        v /= 1024.0;
        ++u;
      }

      // simple formatting: no <iomanip> needed
      char buf[64];
      if (u == 0)
        std::snprintf(buf, sizeof(buf), "%llu %s",
                      static_cast<unsigned long long>(bytes), units[u]);
      else
        std::snprintf(buf, sizeof(buf), "%.2f %s", v, units[u]);
      return std::string(buf);
    }

    struct KeepKey
    {
      std::string pkg;
      std::string commit;

      bool operator==(const KeepKey &o) const noexcept
      {
        return pkg == o.pkg && commit == o.commit;
      }
    };

    struct KeepKeyHash
    {
      std::size_t operator()(const KeepKey &k) const noexcept
      {
        std::hash<std::string> h;
        return (h(k.pkg) * 1315423911u) ^ h(k.commit);
      }
    };

    int store_path()
    {
      vix::cli::util::section(std::cout, "Store");
      vix::cli::util::kv(std::cout, "root", store_root_git().string());
      return 0;
    }

    int store_gc_project()
    {
      const fs::path root = store_root_git();
      const fs::path lockP = lock_path();

      vix::cli::util::section(std::cout, "Store");
      vix::cli::util::kv(std::cout, "action", "gc");
      vix::cli::util::kv(std::cout, "scope", "project");
      vix::cli::util::kv(std::cout, "lock", lockP.string());
      vix::cli::util::kv(std::cout, "root", root.string());

      if (!fs::exists(lockP))
      {
        vix::cli::util::err_line(std::cerr, "missing lock file: " + lockP.string());
        vix::cli::util::warn_line(std::cerr, "Tip: run in a project folder containing vix.lock");
        return 1;
      }

      if (!fs::exists(root))
      {
        vix::cli::util::ok_line(std::cout, "GC done.");
        vix::cli::util::kv(std::cout, "removed commits", "0");
        vix::cli::util::kv(std::cout, "removed packages", "0");
        vix::cli::util::kv(std::cout, "freed", "0 B");
        return 0;
      }

      json lock;
      try
      {
        lock = read_json_or_throw(lockP);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, std::string("failed to read lock: ") + ex.what());
        return 1;
      }

      const json *depsPtr = get_deps_ptr(lock);
      if (!depsPtr)
      {
        vix::cli::util::err_line(std::cerr, "invalid lock: expected array or { dependencies: [] }");
        return 1;
      }

      std::unordered_set<KeepKey, KeepKeyHash> keep;
      for (const auto &d : *depsPtr)
      {
        const std::string id = d.value("id", "");
        const std::string commit = d.value("commit", "");
        const std::string pkg = pkg_dir_from_id(id);
        if (pkg.empty() || commit.empty())
          continue;
        keep.insert(KeepKey{pkg, commit});
      }

      std::size_t removedCommits = 0;
      std::size_t removedPackages = 0;
      std::uintmax_t freed = 0;

      std::error_code ec;
      for (const auto &pkgIt : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec))
      {
        if (ec)
          break;
        if (!pkgIt.is_directory())
          continue;

        const fs::path pkgDir = pkgIt.path();
        const std::string pkgName = pkgDir.filename().string();

        std::vector<fs::path> commitsToRemove;

        std::error_code ec2;
        for (const auto &cIt : fs::directory_iterator(pkgDir, fs::directory_options::skip_permission_denied, ec2))
        {
          if (ec2)
            break;
          if (!cIt.is_directory())
            continue;

          const fs::path commitDir = cIt.path();
          const std::string sha = commitDir.filename().string();

          if (keep.find(KeepKey{pkgName, sha}) == keep.end())
            commitsToRemove.push_back(commitDir);
        }

        for (const auto &cdir : commitsToRemove)
        {
          freed += dir_size_bytes(cdir);

          std::error_code rmec;
          fs::remove_all(cdir, rmec);
          if (!rmec)
            ++removedCommits;
        }

        // If package dir empty -> remove it
        std::error_code ec3;
        bool empty = true;
        for (auto it = fs::directory_iterator(pkgDir, fs::directory_options::skip_permission_denied, ec3);
             it != fs::directory_iterator(); ++it)
        {
          empty = false;
          break;
        }
        if (!ec3 && empty)
        {
          std::error_code rmec2;
          fs::remove(pkgDir, rmec2);
          if (!rmec2)
            ++removedPackages;
        }
      }

      vix::cli::util::ok_line(std::cout, "GC done.");
      vix::cli::util::kv(std::cout, "removed commits", std::to_string(removedCommits));
      vix::cli::util::kv(std::cout, "removed packages", std::to_string(removedPackages));
      vix::cli::util::kv(std::cout, "freed", human_bytes(freed));
      return 0;
    }
  }

  int StoreCommand::run(const std::vector<std::string> &args)
  {
    if (args.empty())
      return help();

    const std::string sub = args[0];

    if (sub == "path")
      return store_path();

    if (sub == "gc")
      return store_gc_project();

    vix::cli::util::err_line(std::cerr, "unknown store subcommand: " + sub);
    vix::cli::util::warn_line(std::cerr, "Try: vix store path");
    vix::cli::util::warn_line(std::cerr, "Try: vix store gc");
    return help();
  }

  int StoreCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix store <subcommand>\n\n"
        << "Subcommands:\n"
        << "  path        Print local store root path\n"
        << "  gc          Garbage collect the store (project scope)\n\n"
        << "Notes:\n"
        << "  - GC scope=project keeps commits referenced by ./vix.lock\n";
    return 0;
  }
}
