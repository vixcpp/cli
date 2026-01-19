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

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

    const json *get_deps_ptr(const json &lock)
    {
      if (lock.is_array())
        return &lock;

      if (lock.is_object() && lock.contains("dependencies") && lock["dependencies"].is_array())
        return &lock["dependencies"];

      return nullptr;
    }

    std::string dep_version(const json &d)
    {
      if (d.contains("version") && d["version"].is_string())
        return d["version"].get<std::string>();

      if (d.contains("tag") && d["tag"].is_string())
      {
        std::string tag = d["tag"].get<std::string>();
        if (!tag.empty() && tag[0] == 'v')
          tag.erase(tag.begin());
        return tag;
      }

      return {};
    }
  }

  int ListCommand::run(const std::vector<std::string> &args)
  {
    (void)args;

    vix::cli::util::section(std::cout, "List");

    const fs::path p = lock_path();
    vix::cli::util::kv(std::cout, "lock", p.string());

    if (!fs::exists(p))
    {
      vix::cli::util::err_line(std::cerr, "missing lock file: " + p.string());
      vix::cli::util::warn_line(std::cerr, "Tip: add deps with: vix add <pkg>@<version>");
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

    const json *depsPtr = get_deps_ptr(lock);
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
    {
      const std::string id = d.value("id", "");
      const std::string ver = dep_version(d);
      const std::string commit = d.value("commit", "");
      std::string repo;
      if (d.contains("repo") && d["repo"].is_object())
        repo = d["repo"].value("url", "");

      vix::cli::util::dep_line(
          std::cout,
          id,
          ver,
          commit,
          repo);

      std::cout << "\n";
    }

    vix::cli::util::ok_line(std::cout, "Found " + std::to_string(deps.size()) + " dependency(ies).");
    return 0;
  }

  int ListCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix list\n\n"
        << "Description:\n"
        << "  List project dependencies from vix.lock.\n\n"
        << "Examples:\n"
        << "  vix list\n";
    return 0;
  }
}
