/**
 *
 *  @file OutdatedCommand.cpp
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
#include <vix/cli/commands/OutdatedCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/util/Semver.hpp>
#include <vix/utils/Env.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
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
      bool jsonOutput{false};
      bool strict{false};
      bool all{false};
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

    struct OutdatedItem
    {
      std::string rawSpec;
      std::string id;
      std::string currentVersion;
      std::string latestVersion;
      std::string movedTo;
      bool outdated{false};
      bool foundInRegistry{false};
    };

    fs::path lock_path()
    {
      return fs::current_path() / "vix.lock";
    }

    fs::path manifest_path()
    {
      return fs::current_path() / "vix.json";
    }

    fs::path home_dir()
    {
#ifdef _WIN32
      const char *home = vix::utils::vix_getenv("USERPROFILE");
#else
      const char *home = vix::utils::vix_getenv("HOME");
#endif
      if (home == nullptr || std::string(home).empty())
      {
        return fs::path();
      }
      return fs::path(home);
    }

    fs::path vix_root()
    {
      const fs::path h = home_dir();
      if (h.empty())
      {
        return fs::path(".vix");
      }
      return h / ".vix";
    }

    fs::path registry_dir()
    {
      return vix_root() / "registry" / "index";
    }

    fs::path registry_index_dir()
    {
      return registry_dir() / "index";
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

    std::string trim_copy(std::string s)
    {
      auto isws = [](unsigned char c)
      { return std::isspace(c) != 0; };

      while (!s.empty() && isws(static_cast<unsigned char>(s.front())))
      {
        s.erase(s.begin());
      }

      while (!s.empty() && isws(static_cast<unsigned char>(s.back())))
      {
        s.pop_back();
      }

      return s;
    }

    json read_manifest_or_throw()
    {
      const fs::path p = manifest_path();
      if (!fs::exists(p))
      {
        throw std::runtime_error("vix.json not found");
      }

      return read_json_or_throw(p);
    }

    std::set<std::string> read_manifest_dependency_ids_or_throw()
    {
      const json manifest = read_manifest_or_throw();

      std::set<std::string> ids;

      if (!manifest.contains("deps") || !manifest["deps"].is_array())
      {
        return ids;
      }

      for (const auto &dep : manifest["deps"])
      {
        if (dep.is_string())
        {
          const std::string id = trim_copy(dep.get<std::string>());
          if (!id.empty())
          {
            ids.insert(id);
          }

          continue;
        }

        if (dep.is_object())
        {
          const std::string id = trim_copy(dep.value("id", ""));
          if (!id.empty())
          {
            ids.insert(id);
          }
        }
      }

      return ids;
    }

    bool ensure_registry_present()
    {
      if (fs::exists(registry_dir()) && fs::exists(registry_index_dir()))
      {
        return true;
      }

      vix::cli::util::err_line(std::cerr, "registry not synced");
      vix::cli::util::warn_line(std::cerr, "Run: vix registry sync");
      return false;
    }

    bool is_flag(const std::string &arg)
    {
      return !arg.empty() && arg[0] == '-';
    }

    bool parse_pkg_spec(const std::string &raw_in, PkgSpec &out)
    {
      const std::string raw = trim_copy(raw_in);

      const auto slash = raw.find('/');
      if (slash == std::string::npos)
      {
        return false;
      }

      if (!raw.empty() && raw[0] == '@')
      {
        if (slash <= 1)
        {
          return false;
        }
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
      {
        return false;
      }

      if (at_version != std::string::npos && out.requestedVersion.empty())
      {
        return false;
      }

      return true;
    }

    int parse_args(const std::vector<std::string> &args, Options &opt)
    {
      for (const auto &arg : args)
      {
        if (arg == "--json")
        {
          opt.jsonOutput = true;
        }
        else if (arg == "--strict")
        {
          opt.strict = true;
        }
        else if (arg == "--all")
        {
          opt.all = true;
        }
        else if (arg == "-h" || arg == "--help")
        {
          return OutdatedCommand::help();
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

    fs::path entry_path(const std::string &ns, const std::string &name)
    {
      return registry_index_dir() / (ns + "." + name + ".json");
    }

    static std::string find_latest_version(const json &entry)
    {
      if (entry.contains("latest") && entry["latest"].is_string())
      {
        return entry["latest"].get<std::string>();
      }

      if (!entry.contains("versions") || !entry["versions"].is_object())
      {
        return {};
      }

      std::vector<std::string> versions;
      versions.reserve(entry["versions"].size());

      for (auto it = entry["versions"].begin(); it != entry["versions"].end(); ++it)
      {
        versions.push_back(it.key());
      }

      return vix::cli::util::semver::findLatest(versions);
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

    std::vector<OutdatedItem> collect_targets(
        const json &lock,
        const std::set<std::string> &directIds,
        const Options &opt,
        int &rc)
    {
      rc = 0;
      std::vector<OutdatedItem> items;

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

          if (id.empty())
          {
            continue;
          }

          if (!opt.all && directIds.find(id) == directIds.end())
          {
            continue;
          }

          OutdatedItem item;
          item.rawSpec = id;
          item.id = id;
          item.currentVersion = version;
          items.push_back(item);
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
          vix::cli::util::warn_line(std::cerr, "Example:  vix outdated gk/jwt");
          vix::cli::util::warn_line(std::cerr, "Example:  vix outdated @gk/jwt@1.x.x");
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

        OutdatedItem item;
        item.rawSpec = raw;
        item.id = id;
        item.currentVersion = read_locked_version(lock, id);
        items.push_back(item);
      }

      return items;
    }

    struct RegistryLookup
    {
      bool found{false};
      std::string latestVersion;
      std::string movedTo;
    };

    RegistryLookup read_registry_lookup(const std::string &packageId)
    {
      RegistryLookup result;

      std::string currentId = packageId;
      std::set<std::string> visited;

      for (int depth = 0; depth < 16; ++depth)
      {
        if (!visited.insert(currentId).second)
        {
          throw std::runtime_error(
              "package migration cycle detected: " + currentId);
        }

        const auto slash = currentId.find('/');

        if (slash == std::string::npos)
        {
          throw std::runtime_error(
              "invalid package id in registry migration: " +
              currentId);
        }

        const std::string ns =
            currentId.substr(0, slash);

        const std::string name =
            currentId.substr(slash + 1);

        const fs::path path =
            entry_path(ns, name);

        if (!fs::exists(path))
        {
          return result;
        }

        const json entry =
            read_json_or_throw(path);

        const std::string movedTo =
            trim_copy(
                entry.value(
                    "movedTo",
                    std::string{}));

        if (!movedTo.empty())
        {
          result.movedTo = movedTo;
          currentId = movedTo;
          continue;
        }

        const std::string latest =
            find_latest_version(entry);

        if (latest.empty())
        {
          return result;
        }

        result.found = true;
        result.latestVersion = latest;
        return result;
      }

      throw std::runtime_error(
          "too many package migrations for: " +
          packageId);
    }

    std::string outdated_status(
        const OutdatedItem &item)
    {
      if (!item.foundInRegistry)
      {
        return "missing";
      }

      if (!item.movedTo.empty())
      {
        return "moved -> " + item.movedTo;
      }

      if (item.outdated)
      {
        return "outdated";
      }

      return "current";
    }

    void print_json_result(
        const std::vector<OutdatedItem> &items)
    {
      json out;
      out["command"] = "outdated";
      out["packages"] = json::array();

      for (const auto &item : items)
      {
        json row;

        row["spec"] = item.rawSpec;
        row["id"] = item.id;
        row["current"] = item.currentVersion;
        row["latest"] = item.latestVersion;
        row["outdated"] = item.outdated;
        row["moved"] = !item.movedTo.empty();
        row["moved_to"] = item.movedTo;
        row["found_in_registry"] =
            item.foundInRegistry;

        out["packages"].push_back(row);
      }

      std::cout << out.dump(2) << "\n";
    }

    void print_table(
        const std::vector<OutdatedItem> &items)
    {
      std::size_t idWidth =
          std::string("Package").size();

      std::size_t currentWidth =
          std::string("Current").size();

      std::size_t latestWidth =
          std::string("Latest").size();

      std::size_t statusWidth =
          std::string("Status").size();

      for (const auto &item : items)
      {
        idWidth =
            std::max(idWidth, item.id.size());

        currentWidth =
            std::max(
                currentWidth,
                item.currentVersion.size());

        latestWidth =
            std::max(
                latestWidth,
                item.latestVersion.size());

        statusWidth =
            std::max(
                statusWidth,
                outdated_status(item).size());
      }

      std::cout
          << std::left
          << std::setw(
                 static_cast<int>(idWidth) + 2)
          << "Package"
          << std::setw(
                 static_cast<int>(currentWidth) + 2)
          << "Current"
          << std::setw(
                 static_cast<int>(latestWidth) + 2)
          << "Latest"
          << std::setw(
                 static_cast<int>(statusWidth) + 2)
          << "Status"
          << "\n";

      for (const auto &item : items)
      {
        const std::string latest =
            item.latestVersion.empty()
                ? "-"
                : item.latestVersion;

        std::cout
            << std::left
            << std::setw(
                   static_cast<int>(idWidth) + 2)
            << item.id
            << std::setw(
                   static_cast<int>(currentWidth) + 2)
            << item.currentVersion
            << std::setw(
                   static_cast<int>(latestWidth) + 2)
            << latest
            << std::setw(
                   static_cast<int>(statusWidth) + 2)
            << outdated_status(item)
            << "\n";
      }
    }

  } // namespace

  int OutdatedCommand::run(const std::vector<std::string> &args)
  {
    try
    {
      Options opt;
      const int parseRc = parse_args(args, opt);
      if (parseRc != 0)
      {
        return parseRc;
      }

      if (!ensure_registry_present())
      {
        return 1;
      }

      const json lock = read_lock_or_throw();

      std::set<std::string> directIds;
      if (!opt.all)
      {
        directIds = read_manifest_dependency_ids_or_throw();
      }

      int targetRc = 0;
      auto items = collect_targets(lock, directIds, opt, targetRc);
      if (targetRc != 0)
      {
        return targetRc;
      }

      if (items.empty())
      {
        if (opt.jsonOutput)
        {
          print_json_result({});
        }
        else
        {
          vix::cli::util::warn_line(std::cout, "no dependencies found");
        }
        return 0;
      }

      for (auto &item : items)
      {
        const RegistryLookup lookup =
            read_registry_lookup(item.id);

        item.movedTo = lookup.movedTo;

        if (lookup.found)
        {
          item.foundInRegistry = true;
          item.latestVersion =
              lookup.latestVersion;

          item.outdated =
              !item.movedTo.empty() ||
              item.currentVersion !=
                  item.latestVersion;
        }
        else
        {
          item.foundInRegistry = false;
          item.latestVersion.clear();
          item.outdated = false;
        }
      }

      if (opt.jsonOutput)
      {
        print_json_result(items);
      }
      else
      {
        vix::cli::util::section(std::cout, "Outdated");
        print_table(items);

        std::size_t outdatedCount = 0;
        std::size_t movedCount = 0;
        std::size_t missingCount = 0;

        for (const auto &item : items)
        {
          if (!item.foundInRegistry)
          {
            ++missingCount;
          }
          else if (!item.movedTo.empty())
          {
            ++movedCount;
          }
          else if (item.outdated)
          {
            ++outdatedCount;
          }
        }

        std::cout << "\n";

        vix::cli::util::ok_line(
            std::cout,
            "checked " +
                std::to_string(items.size()) +
                (opt.all
                     ? " locked package(s), outdated "
                     : " direct package(s), outdated ") +
                std::to_string(outdatedCount) +
                ", moved " +
                std::to_string(movedCount));

        if (missingCount > 0)
        {
          vix::cli::util::warn_line(
              std::cout,
              std::to_string(missingCount) + " package(s) missing from local registry index");
        }
      }

      if (opt.strict)
      {
        for (const auto &item : items)
        {
          if (!item.foundInRegistry || item.outdated)
          {
            return 1;
          }
        }
      }

      return 0;
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(
          std::cerr,
          std::string("outdated failed: ") + ex.what());
      return 1;
    }
  }

  int OutdatedCommand::help()
  {
    std::cout
        << "vix outdated\n"
        << "Check if project dependencies are behind the latest registry versions.\n\n"

        << "Usage\n"
        << "  vix outdated\n"
        << "  vix outdated [@]namespace/name\n"
        << "  vix outdated [@]namespace/name [@]namespace/name\n"
        << "  vix outdated [options]\n\n"

        << "Options\n"
        << "  --json       Print machine-readable JSON output\n"
        << "  --strict     Return exit code 1 if any package is outdated or missing\n"
        << "  -h, --help   Show this help message\n\n"

        << "Examples\n"
        << "  vix outdated\n"
        << "  vix outdated gk/jwt\n"
        << "  vix outdated @gk/jwt\n"
        << "  vix outdated gk/jwt gk/pdf\n"
        << "  vix outdated --json\n"
        << "  vix outdated --strict\n\n"

        << "What happens\n"
        << "  • Reads installed dependencies from vix.lock\n"
        << "  • Looks up the latest version in the local registry index\n"
        << "  • Compares locked versions with registry latest versions\n"
        << "  • Reports outdated or missing packages\n\n"

        << "Notes\n"
        << "  • Comparison is based on vix.lock (installed state), not vix.json\n"
        << "  • Does not resolve dependency ranges\n"
        << "  • Use 'vix update' to upgrade dependencies\n"
        << "  • Run 'vix registry sync' to refresh the local registry index\n"
        << "  • A dependency must exist in vix.lock\n";

    return 0;
  }
}
