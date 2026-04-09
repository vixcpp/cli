/**
 *
 *  @file AddCommand.cpp
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
#include <vix/cli/commands/AddCommand.hpp>

#include <vix/cli/Style.hpp>
#include <vix/cli/util/Lockfile.hpp>
#include <vix/cli/util/Manifest.hpp>
#include <vix/cli/util/Resolver.hpp>
#include <vix/cli/util/Semver.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/utils/Env.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
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
      const std::string home = home_dir();
      if (home.empty())
      {
        return fs::path(".vix");
      }

      return fs::path(home) / ".vix";
    }

    fs::path registry_dir()
    {
      return vix_root() / "registry" / "index";
    }

    fs::path registry_index_dir()
    {
      return registry_dir() / "index";
    }

    fs::path manifest_path()
    {
      return fs::current_path() / "vix.json";
    }

    fs::path lock_path()
    {
      return fs::current_path() / "vix.lock";
    }

    std::string trim_copy(std::string s)
    {
      const auto isws = [](unsigned char c)
      {
        return std::isspace(c) != 0;
      };

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

    std::string to_lower(std::string s)
    {
      std::transform(
          s.begin(),
          s.end(),
          s.begin(),
          [](unsigned char c)
          {
            return static_cast<char>(std::tolower(c));
          });

      return s;
    }

    bool parse_pkg_spec(const std::string &rawInput, PkgSpec &out)
    {
      const std::string raw = trim_copy(rawInput);
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

      const auto atVersion = raw.find('@', slash + 1);

      if (atVersion == std::string::npos)
      {
        out.name = trim_copy(raw.substr(slash + 1));
        out.requestedVersion.clear();
      }
      else
      {
        out.name = trim_copy(raw.substr(slash + 1, atVersion - (slash + 1)));
        out.requestedVersion = trim_copy(raw.substr(atVersion + 1));
      }

      out.resolvedVersion.clear();

      if (out.ns.empty() || out.name.empty())
      {
        return false;
      }

      if (atVersion != std::string::npos && out.requestedVersion.empty())
      {
        return false;
      }

      return true;
    }

    json read_json_file_or_throw(const fs::path &path)
    {
      std::ifstream in(path);
      if (!in)
      {
        throw std::runtime_error("cannot open file: " + path.string());
      }

      json j;
      in >> j;
      return j;
    }

    fs::path entry_path(const std::string &ns, const std::string &name)
    {
      return registry_index_dir() / (ns + "." + name + ".json");
    }

    int ensure_registry_present()
    {
      if (fs::exists(registry_dir()) && fs::exists(registry_index_dir()))
      {
        return 0;
      }

      vix::cli::util::err_line(std::cerr, "registry not synced");
      vix::cli::util::warn_line(std::cerr, "Run: vix registry sync");
      return 1;
    }

    int resolve_version_v1(const json &entry, PkgSpec &spec)
    {
      if (!entry.contains("versions") || !entry["versions"].is_object())
      {
        vix::cli::util::err_line(
            std::cerr,
            "invalid registry entry: missing versions for " + spec.id());
        return 1;
      }

      std::vector<std::string> versions;
      versions.reserve(entry["versions"].size());

      for (auto it = entry["versions"].begin(); it != entry["versions"].end(); ++it)
      {
        versions.push_back(it.key());
      }

      if (versions.empty())
      {
        vix::cli::util::err_line(
            std::cerr,
            "no versions available for: " + spec.id());
        return 1;
      }

      if (spec.requestedVersion.empty())
      {
        spec.resolvedVersion = vix::cli::util::semver::findLatest(versions);
        return 0;
      }

      const auto resolved =
          vix::cli::util::semver::resolveMaxSatisfying(
              versions,
              spec.requestedVersion);

      if (!resolved.has_value())
      {
        vix::cli::util::err_line(
            std::cerr,
            "no version matches range: " + spec.id() + "@" + spec.requestedVersion);
        return 1;
      }

      spec.resolvedVersion = *resolved;
      return 0;
    }

    bool contains_any_icase(std::string hay, const std::string &needleLower)
    {
      if (needleLower.empty())
      {
        return true;
      }

      hay = to_lower(std::move(hay));
      return hay.find(needleLower) != std::string::npos;
    }

    struct SearchHit
    {
      std::string id;
      std::string latest;
      std::string description;
      std::string repo;
    };

    std::vector<SearchHit> search_registry_local(const std::string &queryRaw)
    {
      std::vector<SearchHit> hits;
      const fs::path dir = registry_index_dir();

      if (!fs::exists(dir))
      {
        return hits;
      }

      const std::string query = to_lower(trim_copy(queryRaw));

      for (const auto &entryIt : fs::directory_iterator(dir))
      {
        if (!entryIt.is_regular_file())
        {
          continue;
        }

        const fs::path path = entryIt.path();
        if (path.extension() != ".json")
        {
          continue;
        }

        try
        {
          const json entry = read_json_file_or_throw(path);

          const std::string ns = entry.value("namespace", "");
          const std::string name = entry.value("name", "");
          const std::string id =
              (ns.empty() || name.empty()) ? "" : (ns + "/" + name);

          const std::string description = entry.value("description", "");
          const std::string repo =
              (entry.contains("repo") && entry["repo"].is_object())
                  ? entry["repo"].value("url", "")
                  : "";

          std::string latest;
          if (entry.contains("latest") && entry["latest"].is_string())
          {
            latest = entry["latest"].get<std::string>();
          }

          const std::string hay =
              id + " " + description + " " + repo + " " + path.filename().string();

          if (contains_any_icase(hay, query))
          {
            SearchHit hit;
            hit.id = id;
            hit.latest = latest;
            hit.description = description;
            hit.repo = repo;
            hits.push_back(std::move(hit));
          }
        }
        catch (...)
        {
        }
      }

      hits.erase(
          std::remove_if(
              hits.begin(),
              hits.end(),
              [](const SearchHit &hit)
              {
                return hit.id.empty();
              }),
          hits.end());

      std::sort(
          hits.begin(),
          hits.end(),
          [](const SearchHit &left, const SearchHit &right)
          {
            return left.id < right.id;
          });

      return hits;
    }

    void print_search_hits(const std::vector<SearchHit> &hits, std::size_t limit = 15U)
    {
      if (hits.empty())
      {
        vix::cli::util::warn_line(
            std::cout,
            "No matches found in the local registry index.");
        return;
      }

      const std::size_t count = std::min<std::size_t>(hits.size(), limit);

      for (std::size_t i = 0; i < count; ++i)
      {
        const auto &hit = hits[i];
        vix::cli::util::pkg_line(
            std::cout,
            hit.id,
            hit.latest,
            hit.description,
            hit.repo);
        std::cout << "\n";
      }

      if (hits.size() > limit)
      {
        vix::cli::util::ok_line(
            std::cout,
            "Showing " + std::to_string(count) + " of " +
                std::to_string(hits.size()) + " result(s).");
      }
      else
      {
        vix::cli::util::ok_line(
            std::cout,
            "Found " + std::to_string(hits.size()) + " result(s).");
      }
    }

    std::string find_latest_version(const json &entry)
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

    std::vector<std::string> list_versions(const json &entry)
    {
      std::vector<std::string> out;

      if (!entry.contains("versions") || !entry["versions"].is_object())
      {
        return out;
      }

      for (auto it = entry["versions"].begin(); it != entry["versions"].end(); ++it)
      {
        out.push_back(it.key());
      }

      vix::cli::util::semver::sortAscending(out);
      return out;
    }
  }

  int AddCommand::run(const std::vector<std::string> &args)
  {
    if (args.empty())
    {
      return help();
    }

    if (ensure_registry_present() != 0)
    {
      return 1;
    }

    const std::string raw = args[0];

    PkgSpec spec;
    if (!parse_pkg_spec(raw, spec))
    {
      vix::cli::util::err_line(std::cerr, "invalid package spec");
      vix::cli::util::warn_line(
          std::cerr,
          "Expected: <namespace>/<name>[@<version>]");
      vix::cli::util::warn_line(
          std::cerr,
          "Example:  vix add gaspardkirira/tree");
      vix::cli::util::warn_line(
          std::cerr,
          "Example:  vix add gaspardkirira/tree@0.1.0");
      vix::cli::util::warn_line(
          std::cerr,
          std::string("Try search: vix search ") + raw);
      return 1;
    }

    const fs::path packageEntryPath = entry_path(spec.ns, spec.name);
    if (!fs::exists(packageEntryPath))
    {
      vix::cli::util::err_line(std::cerr, "package not found: " + spec.id());

      vix::cli::util::section(std::cout, "Search");
      vix::cli::util::kv(
          std::cout,
          "query",
          vix::cli::util::quote(spec.name));

      const auto hits = search_registry_local(spec.name);
      print_search_hits(hits);

      vix::cli::util::warn_line(
          std::cerr,
          "If you just updated the registry, run: vix registry sync");
      return 1;
    }

    try
    {
      const json entry = read_json_file_or_throw(packageEntryPath);

      const int resolveRc = resolve_version_v1(entry, spec);
      if (resolveRc != 0)
      {
        return resolveRc;
      }

      if (spec.requestedVersion.empty())
      {
        vix::cli::util::ok_line(
            std::cout,
            "resolved: " + spec.id() + "@" + spec.resolvedVersion);
      }

      const json versions = entry.at("versions");
      if (!versions.contains(spec.resolvedVersion))
      {
        vix::cli::util::err_line(
            std::cerr,
            "version not found: " + spec.id() + "@" + spec.resolvedVersion);

        const std::string latest = find_latest_version(entry);
        const auto allVersions = list_versions(entry);

        if (!allVersions.empty())
        {
          vix::cli::util::section(std::cout, "Available versions");
          for (const auto &version : allVersions)
          {
            std::cout
                << "  "
                << GRAY << "• " << RESET
                << BOLD << version << RESET
                << "\n";
          }
          std::cout << "\n";
        }

        if (!latest.empty())
        {
          vix::cli::util::warn_line(
              std::cerr,
              "Try: vix add " + spec.id() + "@" + latest);
        }

        return 1;
      }

      const json versionNode = versions.at(spec.resolvedVersion);
      const std::string tag = versionNode.at("tag").get<std::string>();
      const std::string commit = versionNode.at("commit").get<std::string>();

      const std::string packageId = spec.id();
      const std::string requested =
          spec.requestedVersion.empty()
              ? spec.resolvedVersion
              : spec.requestedVersion;

      vix::cli::util::manifest::upsert_manifest_dependency_or_throw(
          manifest_path(),
          vix::cli::util::manifest::Dependency{
              packageId,
              requested});

      vix::cli::util::section(std::cout, "Add");
      vix::cli::util::kv(std::cout, "id", packageId);
      vix::cli::util::kv(std::cout, "version", spec.resolvedVersion);
      vix::cli::util::kv(std::cout, "tag", tag);
      vix::cli::util::kv(std::cout, "commit", commit);

      step("resolving project dependencies...");

      const auto manifestDependencies =
          vix::cli::util::manifest::read_manifest_dependencies_or_throw(
              manifest_path());

      const auto lockedDependencies =
          vix::cli::util::resolver::resolve_project_dependencies_or_throw(
              manifestDependencies);

      vix::cli::util::lockfile::write_lockfile_replace_all_or_throw(
          lock_path(),
          lockedDependencies);

      vix::cli::util::ok_line(
          std::cout,
          "added: " + packageId + "@" + spec.resolvedVersion);
      vix::cli::util::ok_line(
          std::cout,
          "lock:  " + lock_path().string());
      vix::cli::util::ok_line(
          std::cout,
          "deps:  " + std::to_string(lockedDependencies.size()));

      return 0;
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(
          std::cerr,
          std::string("add failed: ") + ex.what());
      return 1;
    }
  }

  int AddCommand::help()
  {
    std::cout
        << "vix add\n"
        << "Add a package to your project.\n\n"

        << "Usage\n"
        << "  vix add [@]namespace/name[@version]\n\n"

        << "Examples\n"
        << "  vix add gk/jwt\n"
        << "  vix add gk/jwt@^1.0.0\n"
        << "  vix add @gk/jwt\n"
        << "  vix add @gk/jwt@~1.2.0\n\n"

        << "What happens\n"
        << "  • Resolves the exact version (latest if not specified)\n"
        << "  • Fetches the package at a pinned commit\n"
        << "  • Updates vix.json with the declared dependency\n"
        << "  • Updates vix.lock with the exact resolved version\n"
        << "  • Installs all required dependencies\n\n"

        << "Notes\n"
        << "  • Use 'vix registry sync' if a package is not found\n"
        << "  • '@namespace/name' is supported (scoped packages)\n"
        << "  • vix.json stores declared dependency requirements\n"
        << "  • vix.lock stores exact resolved versions for reproducible installs\n";

    return 0;
  }
}
