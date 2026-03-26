/**
 *
 *  @file UpdateCommand.cpp
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
#include <vix/cli/commands/UpdateCommand.hpp>
#include <vix/cli/commands/AddCommand.hpp>
#include <vix/cli/commands/InstallCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  namespace
  {
    struct Options
    {
      bool dryRun{false};
      bool jsonOutput{false};
      bool installAfter{false};
      std::vector<std::string> rawTargets;
    };

    struct PkgSpec
    {
      std::string ns;
      std::string name;
      std::string requestedVersion;
      std::string resolvedVersion;

      std::string id() const
      {
        return ns + "/" + name;
      }
    };

    struct UpdateItem
    {
      std::string rawSpec;
      std::string id;
      std::string beforeVersion;
      std::string afterVersion;
      bool changed{false};
    };

    fs::path lock_path()
    {
      return fs::current_path() / "vix.lock";
    }

    json read_json_or_throw(const fs::path &p)
    {
      std::ifstream in(p);
      if (!in)
      {
        throw std::runtime_error("cannot open file: " + p.string());
      }

      json j;
      in >> j;
      return j;
    }

    json read_lock_or_throw()
    {
      const fs::path p = lock_path();
      if (!fs::exists(p))
      {
        throw std::runtime_error("vix.lock not found");
      }

      return read_json_or_throw(p);
    }

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

    static bool is_flag(const std::string &arg)
    {
      return !arg.empty() && arg[0] == '-';
    }

    static bool parse_pkg_spec(const std::string &raw_in, PkgSpec &out)
    {
      const std::string raw = trim_copy(raw_in);

      const auto slash = raw.find('/');
      if (slash == std::string::npos)
        return false;

      if (!raw.empty() && raw[0] == '@')
      {
        if (slash <= 1)
          return false;
        out.ns = trim_copy(raw.substr(1, slash - 1));
      }
      else
      {
        out.ns = trim_copy(raw.substr(0, slash));
      }

      const auto at_version = raw.find('@', slash + 1);

      if (at_version == std::string::npos)
      {
        out.name = trim_copy(raw.substr(slash + 1));
        out.requestedVersion.clear();
      }
      else
      {
        out.name = trim_copy(raw.substr(slash + 1, at_version - (slash + 1)));
        out.requestedVersion = trim_copy(raw.substr(at_version + 1));
      }

      out.resolvedVersion.clear();

      if (out.ns.empty() || out.name.empty())
        return false;

      if (at_version != std::string::npos && out.requestedVersion.empty())
        return false;

      return true;
    }

    int parse_args(const std::vector<std::string> &args, Options &opt)
    {
      for (const auto &arg : args)
      {
        if (arg == "--dry-run")
        {
          opt.dryRun = true;
        }
        else if (arg == "--json")
        {
          opt.jsonOutput = true;
        }
        else if (arg == "--install")
        {
          opt.installAfter = true;
        }
        else if (arg == "-h" || arg == "--help")
        {
          return UpdateCommand::help();
        }
        else if (is_flag(arg))
        {
          vix::cli::util::err_line(std::cerr, "unknown option: " + arg);
          return 1;
        }
        else
        {
          opt.rawTargets.push_back(arg);
        }
      }

      return 0;
    }

    bool lock_contains_dependency_id(const json &lock, const std::string &wantedId)
    {
      if (!lock.contains("dependencies") || !lock["dependencies"].is_array())
      {
        return false;
      }

      for (const auto &d : lock["dependencies"])
      {
        if (d.value("id", "") == wantedId)
        {
          return true;
        }
      }

      return false;
    }

    std::string read_locked_version(const json &lock, const std::string &id)
    {
      if (!lock.contains("dependencies") || !lock["dependencies"].is_array())
      {
        return {};
      }

      for (const auto &d : lock["dependencies"])
      {
        if (d.value("id", "") == id)
        {
          return d.value("version", "");
        }
      }

      return {};
    }

    std::vector<UpdateItem> collect_targets(const json &lock, const Options &opt, int &rc)
    {
      rc = 0;
      std::vector<UpdateItem> items;

      if (!lock.contains("dependencies") || !lock["dependencies"].is_array())
      {
        return items;
      }

      if (opt.rawTargets.empty())
      {
        for (const auto &d : lock["dependencies"])
        {
          const std::string id = d.value("id", "");
          const std::string version = d.value("version", "");

          if (!id.empty())
          {
            UpdateItem item;
            item.rawSpec = id;
            item.id = id;
            item.beforeVersion = version;
            items.push_back(item);
          }
        }

        return items;
      }

      std::set<std::string> uniqueRaw(opt.rawTargets.begin(), opt.rawTargets.end());

      for (const auto &raw : uniqueRaw)
      {
        PkgSpec spec;
        if (!parse_pkg_spec(raw, spec))
        {
          vix::cli::util::err_line(std::cerr, "invalid package spec");
          vix::cli::util::warn_line(std::cerr, "Expected: <namespace>/<name>[@<version>]");
          vix::cli::util::warn_line(std::cerr, "Example:  vix update gk/jwt");
          vix::cli::util::warn_line(std::cerr, "Example:  vix update @gk/jwt@1.0.0");
          rc = 1;
          return {};
        }

        const std::string id = spec.id();

        if (!lock_contains_dependency_id(lock, id))
        {
          vix::cli::util::err_line(std::cerr, "dependency not found in vix.lock: " + id);
          rc = 1;
          return {};
        }

        UpdateItem item;
        item.rawSpec = raw;
        item.id = id;
        item.beforeVersion = read_locked_version(lock, id);
        items.push_back(item);
      }

      return items;
    }

    void print_json_result(const std::vector<UpdateItem> &items,
                           bool dryRun,
                           bool installAfter)
    {
      json out;
      out["command"] = "update";
      out["dry_run"] = dryRun;
      out["install_after"] = installAfter;
      out["updated"] = json::array();

      for (const auto &item : items)
      {
        json row;
        row["spec"] = item.rawSpec;
        row["id"] = item.id;
        row["before"] = item.beforeVersion;
        row["after"] = item.afterVersion;
        row["changed"] = item.changed;
        out["updated"].push_back(row);
      }

      std::cout << out.dump(2) << "\n";
    }
  }

  int UpdateCommand::run(const std::vector<std::string> &args)
  {
    try
    {
      Options opt;
      const int parseRc = parse_args(args, opt);
      if (parseRc != 0)
      {
        return parseRc;
      }

      json lock = read_lock_or_throw();

      int targetRc = 0;
      auto items = collect_targets(lock, opt, targetRc);
      if (targetRc != 0)
      {
        return targetRc;
      }

      if (items.empty())
      {
        if (opt.jsonOutput)
        {
          print_json_result({}, opt.dryRun, opt.installAfter);
        }
        else
        {
          vix::cli::util::warn_line(std::cout, "no dependencies to update");
        }
        return 0;
      }

      if (!opt.jsonOutput)
      {
        vix::cli::util::section(std::cout, "Update");
      }

      for (auto &item : items)
      {
        if (!opt.jsonOutput)
        {
          if (opt.dryRun)
          {
            vix::cli::util::step("checking " + item.rawSpec + "...");
          }
          else
          {
            vix::cli::util::step("updating " + item.rawSpec + "...");
          }
        }

        if (!opt.dryRun)
        {
          const int rc = AddCommand::run({item.rawSpec});
          if (rc != 0)
          {
            return rc;
          }
        }

        const json refreshedLock = read_lock_or_throw();
        item.afterVersion = read_locked_version(refreshedLock, item.id);

        if (opt.dryRun)
        {
          item.afterVersion = item.beforeVersion;
          item.changed = false;
        }
        else
        {
          item.changed =
              !item.afterVersion.empty() && item.afterVersion != item.beforeVersion;
        }
      }

      if (opt.installAfter && !opt.dryRun)
      {
        if (!opt.jsonOutput)
        {
          vix::cli::util::step("installing updated dependencies...");
        }

        const int rc = InstallCommand::run({});
        if (rc != 0)
        {
          return rc;
        }
      }

      if (opt.jsonOutput)
      {
        print_json_result(items, opt.dryRun, opt.installAfter);
        return 0;
      }

      std::size_t changedCount = 0;

      for (const auto &item : items)
      {
        if (item.changed)
        {
          changedCount++;
        }

        if (opt.dryRun)
        {
          if (item.rawSpec == item.id)
          {
            vix::cli::util::ok_line(
                std::cout,
                item.id + ": " + item.beforeVersion + " -> latest");
          }
          else
          {
            vix::cli::util::ok_line(
                std::cout,
                item.id + ": " + item.beforeVersion + " -> " + item.rawSpec);
          }
        }
        else
        {
          const std::string after =
              item.afterVersion.empty() ? item.beforeVersion : item.afterVersion;

          vix::cli::util::ok_line(
              std::cout,
              item.id + ": " + item.beforeVersion + " -> " + after);
        }
      }

      vix::cli::util::ok_line(
          std::cout,
          "processed " + std::to_string(items.size()) + " package(s), changed " +
              std::to_string(changedCount));

      if (!opt.installAfter && !opt.dryRun)
      {
        vix::cli::util::warn_line(
            std::cout,
            "Run: vix install to regenerate dependencies");
      }

      return 0;
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(
          std::cerr,
          std::string("update failed: ") + ex.what());
      return 1;
    }
  }

  int UpdateCommand::help()
  {
    std::cout
        << "vix update\n"
        << "Update project dependencies to newer versions.\n\n"

        << "Usage\n"
        << "  vix update\n"
        << "  vix update [@]namespace/name[@version]\n"
        << "  vix update [@]namespace/name[@version] [@]namespace/name[@version]\n"
        << "  vix update [options]\n\n"

        << "Options\n"
        << "  --dry-run    Show what would be updated without changing vix.lock\n"
        << "  --json       Print machine-readable JSON output\n"
        << "  --install    Run 'vix install' after update\n"
        << "  -h, --help   Show this help message\n\n"

        << "Examples\n"
        << "  vix update\n"
        << "  vix update gk/jwt\n"
        << "  vix update @gk/jwt\n"
        << "  vix update gk/jwt@1.0.0\n"
        << "  vix update @gk/jwt@1.x.x\n"
        << "  vix update gk/jwt gk/pdf --install\n"
        << "  vix update --dry-run\n"
        << "  vix update @gk/jwt --json\n\n"

        << "What happens\n"
        << "  • Reads dependencies from vix.lock\n"
        << "  • Updates all packages, or only selected targets\n"
        << "  • Accepts the same package spec syntax as 'vix add'\n"
        << "  • Reuses the normal add flow to rewrite vix.lock\n"
        << "  • Optionally runs 'vix install' at the end\n\n"

        << "Notes\n"
        << "  • This may upgrade major versions\n"
        << "  • A target must already exist in vix.lock\n";

    return 0;
  }
}
