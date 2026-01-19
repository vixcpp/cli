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

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  using namespace vix::cli::style;

  namespace
  {
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

    void write_json_or_throw(const fs::path &p, const json &j)
    {
      std::ofstream out(p);
      if (!out)
        throw std::runtime_error("cannot write: " + p.string());
      out << j.dump(2) << "\n";
    }

    struct Target
    {
      std::string id;
      std::string version;
    };

    Target parse_target(const std::string &raw)
    {
      Target t;
      const auto pos = raw.find('@');
      if (pos == std::string::npos)
      {
        t.id = raw;
        return t;
      }
      t.id = raw.substr(0, pos);
      t.version = raw.substr(pos + 1);
      return t;
    }

    bool matches_id(const std::string &depId, const std::string &targetId)
    {
      return depId == targetId;
    }

    bool matches_version_if_given(const json &dep, const std::string &version)
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
  }

  int RemoveCommand::run(const std::vector<std::string> &args)
  {
    vix::cli::util::section(std::cout, "Remove");

    if (args.empty())
      return help();

    const Target t = parse_target(args[0]);
    vix::cli::util::kv(std::cout, "id", t.id);
    if (!t.version.empty())
      vix::cli::util::kv(std::cout, "version", t.version);

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

    auto &deps = lock["dependencies"];
    const std::size_t before = deps.size();

    json newDeps = json::array();
    bool removed = false;

    for (const auto &d : deps)
    {
      const std::string depId = d.value("id", "");
      if (!removed && matches_id(depId, t.id) && matches_version_if_given(d, t.version))
      {
        removed = true;
        continue;
      }
      newDeps.push_back(d);
    }

    if (!removed)
    {
      vix::cli::util::err_line(std::cerr, "dependency not found in lock: " + t.id);
      vix::cli::util::warn_line(std::cerr, "Tip: run 'vix search <query>' then check what you added");
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

    vix::cli::util::ok_line(std::cout, "removed: " + t.id);
    vix::cli::util::ok_line(std::cout, "lock:  " + p.string());

    vix::cli::util::warn_line(
        std::cout,
        "Note: 'vix search' searches the registry index, not your project dependencies.");

    vix::cli::util::warn_line(
        std::cout,
        "Tip: use 'vix list' to view current project dependencies.");

    const std::size_t after = lock["dependencies"].size();
    (void)before;
    (void)after;

    return 0;
  }

  int RemoveCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix remove <pkg>\n"
        << "  vix remove <pkg>@<version>\n\n"
        << "Description:\n"
        << "  Remove a dependency from the local vix.lock.\n\n"
        << "Examples:\n"
        << "  vix remove gaspardkirira/tree\n"
        << "  vix remove gaspardkirira/tree@0.1.0\n";
    return 0;
  }
}
