/**
 *
 *  @file RemoveCommand.cpp
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
#include <vix/cli/commands/RemoveCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/Style.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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
    struct Target
    {
      std::string id;      // namespace/name
      std::string version; // optional
    };

    struct Options
    {
      Target target;
      bool yes{false};
      bool purge{false}; // delete .vix/deps/<ns>.<name>
    };

    static std::string trim_copy(std::string s)
    {
      auto isws = [](unsigned char c)
      { return std::isspace(c) != 0; };
      while (!s.empty() && isws(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
      while (!s.empty() && isws(static_cast<unsigned char>(s.back())))
        s.pop_back();
      return s;
    }

    static Target parse_target(std::string raw)
    {
      raw = trim_copy(raw);

      Target t;
      const auto pos = raw.find('@');
      if (pos == std::string::npos)
      {
        t.id = raw;
        return t;
      }
      t.id = raw.substr(0, pos);
      t.version = raw.substr(pos + 1);
      t.id = trim_copy(t.id);
      t.version = trim_copy(t.version);
      return t;
    }

    static fs::path lock_path()
    {
      return fs::current_path() / "vix.lock";
    }

    static json read_json_or_throw(const fs::path &p)
    {
      std::ifstream in(p);
      if (!in)
        throw std::runtime_error("cannot open: " + p.string());
      json j;
      in >> j;
      return j;
    }

    static void write_json_or_throw(const fs::path &p, const json &j)
    {
      std::ofstream out(p);
      if (!out)
        throw std::runtime_error("cannot write: " + p.string());
      out << j.dump(2) << "\n";
    }

    static bool matches_id(const std::string &depId, const std::string &targetId)
    {
      return depId == targetId;
    }

    static bool matches_version_if_given(const json &dep, const std::string &version)
    {
      if (version.empty())
        return true;

      if (dep.contains("version") && dep["version"].is_string())
        return dep["version"].get<std::string>() == version;

      if (dep.contains("tag") && dep["tag"].is_string())
      {
        const std::string tag = dep["tag"].get<std::string>(); // e.g. v0.1.0
        if (!tag.empty() && tag[0] == 'v')
          return tag.substr(1) == version;
        return tag == version;
      }

      return false;
    }

    static std::optional<std::pair<std::string, std::string>> split_pkg_id(const std::string &id)
    {
      const auto pos = id.find('/');
      if (pos == std::string::npos)
        return std::nullopt;

      std::string ns = trim_copy(id.substr(0, pos));
      std::string name = trim_copy(id.substr(pos + 1));

      if (ns.empty() || name.empty())
        return std::nullopt;

      // keep exact casing in display, but your ids are already lower-case
      return std::make_pair(ns, name);
    }

    static fs::path deps_dir_for_pkg(const std::string &pkgId)
    {
      // Layout: .vix/deps/<namespace>.<name>
      const auto parts = split_pkg_id(pkgId);
      if (!parts)
        return fs::path();

      const std::string folder = parts->first + "." + parts->second;
      return fs::current_path() / ".vix" / "deps" / folder;
    }

    static bool confirm_delete_if_needed(const Options &opt, const fs::path &depDir)
    {
      if (!opt.purge)
        return true;

      if (depDir.empty() || !fs::exists(depDir))
        return true;

      if (opt.yes)
        return true;

      vix::cli::util::warn_line(std::cout, "This will also delete files from this project:");
      vix::cli::util::info_line(std::cout, depDir.string());
      vix::cli::util::warn_line(std::cout, "Type DELETE to confirm: ");

      std::string line;
      std::getline(std::cin, line);
      line = trim_copy(line);
      return line == "DELETE";
    }

    static Options parse_args_or_throw(const std::vector<std::string> &args)
    {
      if (args.empty())
        throw std::runtime_error("missing package id. Try: vix remove namespace/name");

      Options opt;
      std::vector<std::string> pos;

      for (size_t i = 0; i < args.size(); ++i)
      {
        const auto &a = args[i];

        if (a == "-y" || a == "--yes")
        {
          opt.yes = true;
          continue;
        }
        if (a == "--purge")
        {
          opt.purge = true;
          continue;
        }
        if (!a.empty() && a[0] == '-')
          throw std::runtime_error("unknown flag: " + a);

        pos.push_back(a);
      }

      if (pos.empty())
        throw std::runtime_error("missing package id");

      opt.target = parse_target(pos[0]);
      if (opt.target.id.empty())
        throw std::runtime_error("package id cannot be empty");

      return opt;
    }

  } // namespace

  int RemoveCommand::run(const std::vector<std::string> &args)
  {
    vix::cli::util::section(std::cout, "Remove");

    Options opt;
    try
    {
      opt = parse_args_or_throw(args);
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(std::cerr, ex.what());
      return help();
    }

    vix::cli::util::kv(std::cout, "id", opt.target.id);
    if (!opt.target.version.empty())
      vix::cli::util::kv(std::cout, "version", opt.target.version);

    const fs::path p = lock_path();
    if (!fs::exists(p))
    {
      vix::cli::util::err_line(std::cerr, "missing lock file: " + p.string());
      vix::cli::util::warn_line(std::cerr, "Run: vix add <pkg>@<version> first");
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

    if (!lock.contains("dependencies") || !lock["dependencies"].is_array())
    {
      vix::cli::util::err_line(std::cerr, "invalid lock: missing 'dependencies' array");
      vix::cli::util::warn_line(std::cerr, "Tip: regenerate lock by re-adding dependencies");
      return 1;
    }

    const fs::path depDir = deps_dir_for_pkg(opt.target.id);

    if (!confirm_delete_if_needed(opt, depDir))
    {
      vix::cli::util::warn_line(std::cout, "cancelled");
      return 0;
    }

    auto &deps = lock["dependencies"];

    json newDeps = json::array();
    bool removed = false;

    for (const auto &d : deps)
    {
      const std::string depId = d.value("id", "");
      if (!removed && matches_id(depId, opt.target.id) && matches_version_if_given(d, opt.target.version))
      {
        removed = true;
        continue;
      }
      newDeps.push_back(d);
    }

    if (!removed)
    {
      vix::cli::util::err_line(std::cerr, "dependency not found in lock: " + opt.target.id);
      vix::cli::util::warn_line(std::cerr, "Tip: use 'vix list' to check current deps");
      return 1;
    }

    lock["dependencies"] = std::move(newDeps);

    try
    {
      write_json_or_throw(p, lock);
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(std::cerr, std::string("failed to write lock: ") + ex.what());
      return 1;
    }

    if (opt.purge && !depDir.empty() && fs::exists(depDir))
    {
      std::error_code ec;
      fs::remove_all(depDir, ec);
      if (ec)
        vix::cli::util::warn_line(std::cout, "failed to delete: " + depDir.string());
      else
        vix::cli::util::ok_line(std::cout, "deleted: " + depDir.string());
    }

    vix::cli::util::ok_line(std::cout, "removed from vix.lock: " + opt.target.id);
    vix::cli::util::ok_line(std::cout, "lock:  " + p.string());

    vix::cli::util::warn_line(std::cout, "Tip: run 'vix deps' to regenerate .vix/vix_deps.cmake if needed.");
    return 0;
  }

  int RemoveCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix remove <pkg>\n"
        << "  vix remove <pkg>@<version>\n"
        << "  vix remove <pkg> --purge [-y|--yes]\n\n"

        << "Description:\n"
        << "  Remove a dependency from the current project.\n\n"

        << "Options:\n"
        << "  --purge        Also delete the local package directory (.vix/deps/<namespace>.<name>)\n"
        << "  -y, --yes      Skip confirmation (used with --purge)\n\n"

        << "Notes:\n"
        << "  - This command only affects the current project.\n"
        << "  - The registry is never modified.\n\n"

        << "Examples:\n"
        << "  vix remove gaspardkirira/strings\n"
        << "  vix remove gaspardkirira/strings@0.1.4\n"
        << "  vix remove gaspardkirira/strings --purge\n";

    return 0;
  }
} // namespace vix::commands
