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
      bool all{false};
      bool jsonOutput{false};
      bool help{false};
      std::size_t page{1};
      std::size_t limit{20};
    };

    fs::path lock_path()
    {
      return fs::current_path() / "vix.lock";
    }

    fs::path manifest_path()
    {
      return fs::current_path() / "vix.json";
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
      if (const char *p = vix::utils::vix_getenv("VIX_GLOBAL_PREFIX"); p && *p)
        return fs::path(p);

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

    std::string trim_copy(std::string s)
    {
      auto isws = [](unsigned char c)
      {
        return std::isspace(c) != 0;
      };

      while (!s.empty() && isws(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());

      while (!s.empty() && isws(static_cast<unsigned char>(s.back())))
        s.pop_back();

      return s;
    }

    bool parse_positive_size(const std::string &s, std::size_t &out)
    {
      if (s.empty())
        return false;

      std::size_t value = 0;

      for (char c : s)
      {
        if (c < '0' || c > '9')
          return false;

        value = (value * 10u) + static_cast<std::size_t>(c - '0');
      }

      if (value == 0)
        return false;

      out = value;
      return true;
    }

    const json *get_project_deps_ptr(const json &lock)
    {
      if (lock.is_array())
        return &lock;

      if (lock.is_object() &&
          lock.contains("dependencies") &&
          lock["dependencies"].is_array())
      {
        return &lock["dependencies"];
      }

      return nullptr;
    }

    const json *get_global_pkgs_ptr(const json &root)
    {
      if (root.is_object() &&
          root.contains("packages") &&
          root["packages"].is_array())
      {
        return &root["packages"];
      }

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

    bool parse_args(const std::vector<std::string> &args, ParsedArgs &parsed)
    {
      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &arg = args[i];

        if (arg == "-g" || arg == "--global")
        {
          parsed.globalMode = true;
          continue;
        }

        if (arg == "--all")
        {
          parsed.all = true;
          continue;
        }

        if (arg == "--json")
        {
          parsed.jsonOutput = true;
          continue;
        }

        if (arg == "--page")
        {
          if (i + 1 >= args.size())
            return false;

          std::size_t page = 0;
          if (!parse_positive_size(args[++i], page))
            return false;

          parsed.page = page;
          continue;
        }

        if (arg == "--limit")
        {
          if (i + 1 >= args.size())
            return false;

          std::size_t limit = 0;
          if (!parse_positive_size(args[++i], limit))
            return false;

          parsed.limit = std::clamp<std::size_t>(limit, 1, 100);
          continue;
        }

        if (arg.rfind("--page=", 0) == 0)
        {
          std::size_t page = 0;
          if (!parse_positive_size(arg.substr(7), page))
            return false;

          parsed.page = page;
          continue;
        }

        if (arg.rfind("--limit=", 0) == 0)
        {
          std::size_t limit = 0;
          if (!parse_positive_size(arg.substr(8), limit))
            return false;

          parsed.limit = std::clamp<std::size_t>(limit, 1, 100);
          continue;
        }

        if (arg == "-h" || arg == "--help")
        {
          parsed.help = true;
          continue;
        }

        if (!arg.empty() && arg[0] == '-')
        {
          vix::cli::util::err_line(std::cerr, "unknown option: " + arg);
          return false;
        }

        vix::cli::util::err_line(std::cerr, "unexpected argument: " + arg);
        return false;
      }

      return true;
    }

    std::vector<std::string> read_manifest_dependency_ids_or_throw()
    {
      const fs::path p = manifest_path();

      if (!fs::exists(p))
      {
        throw std::runtime_error("vix.json not found");
      }

      const json manifest = read_json_or_throw(p);

      std::vector<std::string> ids;

      if (!manifest.contains("deps") || !manifest["deps"].is_array())
      {
        return ids;
      }

      for (const auto &dep : manifest["deps"])
      {
        std::string id;

        if (dep.is_string())
        {
          id = trim_copy(dep.get<std::string>());
        }
        else if (dep.is_object())
        {
          id = trim_copy(dep.value("id", ""));
        }

        if (!id.empty() &&
            std::find(ids.begin(), ids.end(), id) == ids.end())
        {
          ids.push_back(id);
        }
      }

      return ids;
    }

    const json *find_locked_dep_by_id(const json &deps, const std::string &id)
    {
      if (!deps.is_array())
        return nullptr;

      for (const auto &dep : deps)
      {
        if (dep.value("id", "") == id)
          return &dep;
      }

      return nullptr;
    }

    bool page_bounds(
        std::size_t total,
        std::size_t page,
        std::size_t limit,
        std::size_t &start,
        std::size_t &end,
        std::size_t &totalPages)
    {
      if (total == 0)
      {
        start = 0;
        end = 0;
        totalPages = 0;
        return true;
      }

      totalPages = (total + limit - 1) / limit;

      if (page > totalPages)
        return false;

      start = (page - 1) * limit;
      end = std::min(start + limit, total);

      return true;
    }

    json dep_to_json(const json &d, bool globalMode)
    {
      json row = json::object();

      row["id"] = d.value("id", "");
      row["version"] = dep_version(d);
      row["commit"] = d.value("commit", "");

      if (d.contains("repo"))
      {
        if (d["repo"].is_object())
        {
          row["repo"] = d["repo"].value("url", "");
        }
        else if (d["repo"].is_string())
        {
          row["repo"] = d["repo"].get<std::string>();
        }
        else
        {
          row["repo"] = "";
        }
      }
      else
      {
        row["repo"] = "";
      }

      if (globalMode)
      {
        row["type"] = d.value("type", "");
        row["include"] = d.value("include", "");
        row["installed_path"] = d.value("installed_path", "");
        row["prefix"] = d.value("prefix", "");
        row["executables"] = d.contains("executables") ? d["executables"] : json::array();
      }

      return row;
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

        if (!type.empty())
          vix::cli::util::kv(std::cout, "type", type);

        if (!include.empty())
          vix::cli::util::kv(std::cout, "include", include);

        if (d.contains("executables") && d["executables"].is_array() && !d["executables"].empty())
        {
          std::string commands;
          for (const auto &exe : d["executables"])
          {
            if (!exe.is_string())
              continue;
            if (!commands.empty())
              commands += ", ";
            commands += exe.get<std::string>();
          }
          if (!commands.empty())
            vix::cli::util::kv(std::cout, "commands", commands);
        }
      }

      std::cout << "\n";
    }

    void print_json_page(
        const std::vector<const json *> &items,
        const ParsedArgs &parsed,
        const std::string &scope,
        bool globalMode)
    {
      std::size_t start = 0;
      std::size_t end = 0;
      std::size_t totalPages = 0;

      const bool ok = page_bounds(
          items.size(),
          parsed.page,
          parsed.limit,
          start,
          end,
          totalPages);

      json out = json::object();
      out["command"] = "list";
      out["global"] = globalMode;
      out["scope"] = scope;
      out["page"] = parsed.page;
      out["limit"] = parsed.limit;
      out["total"] = items.size();
      out["total_pages"] = totalPages;
      out["items"] = json::array();

      if (!ok)
      {
        out["error"] = "page out of range";
        std::cout << out.dump(2) << "\n";
        return;
      }

      for (std::size_t i = start; i < end; ++i)
      {
        out["items"].push_back(dep_to_json(*items[i], globalMode));
      }

      std::cout << out.dump(2) << "\n";
    }

    int print_paged_blocks(
        const std::vector<const json *> &items,
        const ParsedArgs &parsed,
        const std::string &scope,
        bool globalMode)
    {
      std::size_t start = 0;
      std::size_t end = 0;
      std::size_t totalPages = 0;

      if (!page_bounds(items.size(), parsed.page, parsed.limit, start, end, totalPages))
      {
        vix::cli::util::err_line(std::cerr, "page out of range");
        vix::cli::util::warn_line(std::cerr, "Total pages: " + std::to_string(totalPages));
        return 1;
      }

      std::cout << "\n";

      for (std::size_t i = start; i < end; ++i)
      {
        print_dep_block(*items[i], globalMode);
      }

      vix::cli::util::ok_line(
          std::cout,
          "Showing " + std::to_string(start + 1) +
              "-" + std::to_string(end) +
              " of " + std::to_string(items.size()) +
              " " + scope + ".");

      if (totalPages > 1)
      {
        std::cout << "  " << GRAY
                  << "Page " << parsed.page << "/" << totalPages
                  << RESET << "\n";

        if (parsed.page < totalPages)
        {
          std::cout << "  " << GRAY
                    << "Next: vix list ";

          if (globalMode)
            std::cout << "-g ";

          if (scope.find("locked") != std::string::npos)
            std::cout << "--all ";

          std::cout << "--page " << (parsed.page + 1)
                    << " --limit " << parsed.limit
                    << RESET << "\n";
        }
      }

      return 0;
    }

  } // namespace

  int ListCommand::run(const std::vector<std::string> &args)
  {
    ParsedArgs parsed;

    if (!parse_args(args, parsed))
    {
      vix::cli::util::warn_line(
          std::cerr,
          "Usage: vix list [--all] [--page N] [--limit N] [--json]");
      return 1;
    }

    if (parsed.help)
    {
      return help();
    }

    if (parsed.globalMode)
    {
      if (!parsed.jsonOutput)
      {
        vix::cli::util::section(std::cout, "Global packages");
      }

      const fs::path p = global_manifest_path();

      if (!fs::exists(p))
      {
        if (parsed.jsonOutput)
        {
          json out = json::object();
          out["command"] = "list";
          out["global"] = true;
          out["scope"] = "global packages";
          out["page"] = parsed.page;
          out["limit"] = parsed.limit;
          out["total"] = 0;
          out["total_pages"] = 0;
          out["items"] = json::array();
          std::cout << out.dump(2) << "\n";
        }
        else
        {
          vix::cli::util::ok_line(std::cout, "No global packages installed.");
        }

        return 0;
      }

      json root;
      try
      {
        root = read_json_or_throw(p);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(
            std::cerr,
            std::string("failed to read manifest: ") + ex.what());
        return 1;
      }

      const json *pkgsPtr = get_global_pkgs_ptr(root);
      if (!pkgsPtr)
      {
        vix::cli::util::err_line(
            std::cerr,
            "invalid global manifest: expected { packages: [] }");
        return 1;
      }

      const json &pkgs = *pkgsPtr;

      std::vector<const json *> visible;
      visible.reserve(pkgs.size());

      for (const auto &pkg : pkgs)
      {
        visible.push_back(&pkg);
      }

      if (visible.empty())
      {
        if (parsed.jsonOutput)
        {
          print_json_page(visible, parsed, "global packages", true);
        }
        else
        {
          vix::cli::util::ok_line(std::cout, "No global packages installed.");
        }

        return 0;
      }

      if (parsed.jsonOutput)
      {
        print_json_page(visible, parsed, "global packages", true);
        return 0;
      }

      return print_paged_blocks(
          visible,
          parsed,
          "global package(s)",
          true);
    }

    if (!parsed.jsonOutput)
    {
      vix::cli::util::section(std::cout, "Project dependencies");
    }

    const fs::path p = lock_path();

    if (!parsed.jsonOutput)
    {
      vix::cli::util::kv(std::cout, "lock", p.string());
    }

    if (!fs::exists(p))
    {
      vix::cli::util::err_line(std::cerr, "missing lock file: " + p.string());
      vix::cli::util::warn_line(
          std::cerr,
          "Tip: add a package with: vix add <pkg>@<version>");
      return 1;
    }

    json lock;
    try
    {
      lock = read_json_or_throw(p);
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(
          std::cerr,
          std::string("failed to read lock: ") + ex.what());
      return 1;
    }

    const json *depsPtr = get_project_deps_ptr(lock);
    if (!depsPtr)
    {
      vix::cli::util::err_line(
          std::cerr,
          "invalid lock: expected array or { dependencies: [] }");
      return 1;
    }

    const json &deps = *depsPtr;

    std::vector<const json *> visibleDeps;

    if (parsed.all)
    {
      visibleDeps.reserve(deps.size());

      for (const auto &d : deps)
      {
        visibleDeps.push_back(&d);
      }
    }
    else
    {
      std::vector<std::string> directIds;

      try
      {
        directIds = read_manifest_dependency_ids_or_throw();
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(
            std::cerr,
            std::string("failed to read vix.json: ") + ex.what());
        return 1;
      }

      visibleDeps.reserve(directIds.size());

      for (const auto &id : directIds)
      {
        const json *dep = find_locked_dep_by_id(deps, id);
        if (dep != nullptr)
        {
          visibleDeps.push_back(dep);
        }
      }
    }

    if (visibleDeps.empty())
    {
      if (parsed.jsonOutput)
      {
        print_json_page(
            visibleDeps,
            parsed,
            parsed.all ? "locked dependencies" : "direct dependencies",
            false);
      }
      else
      {
        vix::cli::util::ok_line(std::cout, "No dependencies.");
      }

      return 0;
    }

    if (parsed.jsonOutput)
    {
      print_json_page(
          visibleDeps,
          parsed,
          parsed.all ? "locked dependencies" : "direct dependencies",
          false);
      return 0;
    }

    return print_paged_blocks(
        visibleDeps,
        parsed,
        parsed.all ? "locked dependency(ies)" : "direct dependency(ies)",
        false);
  }

  int ListCommand::help()
  {
    std::cout
        << "vix list\n"
        << "List project dependencies or globally installed packages.\n\n"

        << "Usage\n"
        << "  vix list [--page N] [--limit N]\n"
        << "  vix list --all [--page N] [--limit N]\n"
        << "  vix list -g [--page N] [--limit N]\n"
        << "  vix list --json\n\n"

        << "Options\n"
        << "  --all        Include transitive locked dependencies\n"
        << "  --page N     Show page N\n"
        << "  --limit N    Limit items per page, max 100\n"
        << "  --json       Print machine-readable JSON output\n"
        << "  -g, --global List globally installed packages\n"
        << "  -h, --help   Show this help message\n\n"

        << "Examples\n"
        << "  vix list\n"
        << "  vix list --all\n"
        << "  vix list --page 2 --limit 10\n"
        << "  vix list --all --page 2 --limit 10\n"
        << "  vix list -g --limit 50\n"
        << "  vix list --json\n\n"

        << "What happens\n"
        << "  • 'vix list' shows direct project dependencies from vix.json\n"
        << "  • 'vix list --all' shows all locked dependencies from vix.lock\n"
        << "  • 'vix list -g' reads packages from ~/.vix/global/installed.json\n";

    return 0;
  }
}
