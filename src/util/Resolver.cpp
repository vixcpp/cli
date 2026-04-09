/**
 *
 *  @file Resolver.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/util/Resolver.hpp>

#include <vix/cli/util/Hash.hpp>
#include <vix/cli/util/Semver.hpp>
#include <vix/cli/util/Shell.hpp>
#include <vix/utils/Env.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::cli::util::resolver
{
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
      const std::string h = home_dir();
      if (h.empty())
      {
        return fs::path(".vix");
      }

      return fs::path(h) / ".vix";
    }

    fs::path registry_dir()
    {
      return vix_root() / "registry" / "index";
    }

    fs::path registry_index_dir()
    {
      return registry_dir() / "index";
    }

    fs::path store_git_dir()
    {
      return vix_root() / "store" / "git";
    }

    std::string trim_copy(std::string s)
    {
      auto isws = [](unsigned char c)
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

    void ensure_registry_present_or_throw()
    {
      if (fs::exists(registry_dir()) && fs::exists(registry_index_dir()))
      {
        return;
      }

      throw std::runtime_error("registry not synced");
    }

    std::optional<PkgSpec> parse_dependency_spec(
        const vix::cli::util::manifest::Dependency &dependency)
    {
      const std::string id = trim_copy(dependency.id);
      const auto slash = id.find('/');

      if (slash == std::string::npos)
      {
        return std::nullopt;
      }

      PkgSpec spec;
      spec.ns = trim_copy(id.substr(0, slash));
      spec.name = trim_copy(id.substr(slash + 1));
      spec.requestedVersion = trim_copy(dependency.requested);
      spec.resolvedVersion.clear();

      if (spec.ns.empty() || spec.name.empty())
      {
        return std::nullopt;
      }

      return spec;
    }

    int resolve_version_or_throw(const json &entry, PkgSpec &spec)
    {
      if (!entry.contains("versions") || !entry["versions"].is_object())
      {
        throw std::runtime_error(
            "invalid registry entry: missing versions for " + spec.id());
      }

      std::vector<std::string> versions;
      versions.reserve(entry["versions"].size());

      for (auto it = entry["versions"].begin(); it != entry["versions"].end(); ++it)
      {
        versions.push_back(it.key());
      }

      if (versions.empty())
      {
        throw std::runtime_error("no versions available for: " + spec.id());
      }

      if (spec.requestedVersion.empty())
      {
        spec.resolvedVersion = vix::cli::util::semver::findLatest(versions);
        return 0;
      }

      const auto resolved =
          vix::cli::util::semver::resolveMaxSatisfying(versions, spec.requestedVersion);

      if (!resolved.has_value())
      {
        throw std::runtime_error(
            "no version matches range: " + spec.id() + "@" + spec.requestedVersion);
      }

      spec.resolvedVersion = *resolved;
      return 0;
    }

    int clone_checkout_or_throw(
        const std::string &repoUrl,
        const std::string &idDot,
        const std::string &commit,
        std::string &outDir)
    {
      fs::create_directories(store_git_dir());

      const fs::path dst = store_git_dir() / idDot / commit;
      outDir = dst.string();

      if (fs::exists(dst))
      {
        return 0;
      }

      fs::create_directories(dst.parent_path());

      {
        const std::string cmd =
            "git clone -q " + repoUrl + " " + dst.string();
        const int rc = vix::cli::util::run_cmd_retry_debug(cmd);
        if (rc != 0)
        {
          throw std::runtime_error("git clone failed for: " + repoUrl);
        }
      }

      {
        const std::string cmd =
            "git -C " + dst.string() +
            " -c advice.detachedHead=false checkout -q " + commit;
        const int rc = vix::cli::util::run_cmd_retry_debug(cmd);
        if (rc != 0)
        {
          throw std::runtime_error("git checkout failed for commit: " + commit);
        }
      }

      return 0;
    }

    std::optional<PkgSpec> parse_dep_string_v1(const std::string &raw)
    {
      const std::string s = trim_copy(raw);
      if (s.empty())
      {
        return std::nullopt;
      }

      const auto slash = s.find('/');
      if (slash == std::string::npos)
      {
        return std::nullopt;
      }

      const auto at = s.find('@', slash + 1);

      PkgSpec spec;
      spec.ns = trim_copy(s.substr(0, slash));

      if (at == std::string::npos)
      {
        spec.name = trim_copy(s.substr(slash + 1));
        spec.requestedVersion.clear();
      }
      else
      {
        spec.name = trim_copy(s.substr(slash + 1, at - (slash + 1)));
        spec.requestedVersion = trim_copy(s.substr(at + 1));
      }

      spec.resolvedVersion.clear();

      if (spec.ns.empty() || spec.name.empty())
      {
        return std::nullopt;
      }

      return spec;
    }

    std::optional<PkgSpec> parse_dep_obj_v1(const json &dependency)
    {
      if (!dependency.is_object())
      {
        return std::nullopt;
      }

      const std::string id = trim_copy(dependency.value("id", ""));
      if (id.empty())
      {
        return std::nullopt;
      }

      std::string requested = trim_copy(dependency.value("version", ""));
      if (requested.empty())
      {
        requested = trim_copy(dependency.value("requested", ""));
      }
      if (requested.empty())
      {
        requested = trim_copy(dependency.value("range", ""));
      }

      const auto slash = id.find('/');
      if (slash == std::string::npos)
      {
        return std::nullopt;
      }

      PkgSpec spec;
      spec.ns = trim_copy(id.substr(0, slash));
      spec.name = trim_copy(id.substr(slash + 1));
      spec.requestedVersion = requested;
      spec.resolvedVersion.clear();

      if (spec.ns.empty() || spec.name.empty())
      {
        return std::nullopt;
      }

      return spec;
    }

    std::vector<PkgSpec> read_vix_json_deps_v1(const fs::path &repoDir)
    {
      std::vector<PkgSpec> out;

      const fs::path manifestPath = repoDir / "vix.json";
      if (!fs::exists(manifestPath))
      {
        return out;
      }

      json root;
      try
      {
        root = read_json_file_or_throw(manifestPath);
      }
      catch (...)
      {
        return out;
      }

      if (!root.is_object())
      {
        return out;
      }

      if (!root.contains("deps") || !root["deps"].is_array())
      {
        return out;
      }

      for (const auto &dependency : root["deps"])
      {
        if (dependency.is_object())
        {
          auto spec = parse_dep_obj_v1(dependency);
          if (spec.has_value())
          {
            out.push_back(*spec);
          }
          continue;
        }

        if (dependency.is_string())
        {
          auto spec = parse_dep_string_v1(dependency.get<std::string>());
          if (spec.has_value())
          {
            out.push_back(*spec);
          }
        }
      }

      return out;
    }

    void upsert_locked_dependency(
        std::vector<vix::cli::util::lockfile::LockedDependency> &dependencies,
        const vix::cli::util::lockfile::LockedDependency &dependency)
    {
      for (auto &item : dependencies)
      {
        if (item.id == dependency.id)
        {
          item = dependency;
          return;
        }
      }

      dependencies.push_back(dependency);
    }

    void resolve_transitive_or_throw(
        PkgSpec spec,
        std::unordered_set<std::string> &visited,
        std::vector<vix::cli::util::lockfile::LockedDependency> &lockedDependencies)
    {
      const fs::path registryEntryPath = entry_path(spec.ns, spec.name);
      if (!fs::exists(registryEntryPath))
      {
        throw std::runtime_error("package not found: " + spec.id());
      }

      const json entry = read_json_file_or_throw(registryEntryPath);

      resolve_version_or_throw(entry, spec);

      const json versions = entry.at("versions");
      if (!versions.contains(spec.resolvedVersion))
      {
        throw std::runtime_error(
            "version not found: " + spec.id() + "@" + spec.resolvedVersion);
      }

      const json versionNode = versions.at(spec.resolvedVersion);
      const std::string repoUrl = entry.at("repo").at("url").get<std::string>();
      const std::string tag = versionNode.at("tag").get<std::string>();
      const std::string commit = versionNode.at("commit").get<std::string>();

      const std::string visitKey = spec.id() + "@" + spec.resolvedVersion;
      if (visited.count(visitKey))
      {
        return;
      }
      visited.insert(visitKey);

      const std::string idDot = spec.ns + "." + spec.name;

      std::string installedDir;
      clone_checkout_or_throw(repoUrl, idDot, commit, installedDir);

      const auto contentHash = vix::cli::util::sha256_directory(installedDir);
      const std::string hashStr = contentHash.value_or("");

      upsert_locked_dependency(
          lockedDependencies,
          vix::cli::util::lockfile::LockedDependency{
              spec.id(),
              spec.requestedVersion.empty() ? spec.resolvedVersion : spec.requestedVersion,
              spec.resolvedVersion,
              repoUrl,
              tag,
              commit,
              hashStr});

      const auto transitiveDependencies = read_vix_json_deps_v1(fs::path(installedDir));
      for (auto transitiveSpec : transitiveDependencies)
      {
        resolve_transitive_or_throw(transitiveSpec, visited, lockedDependencies);
      }
    }
  }

  std::vector<vix::cli::util::lockfile::LockedDependency>
  resolve_project_dependencies_or_throw(
      const std::vector<vix::cli::util::manifest::Dependency> &manifestDependencies)
  {
    ensure_registry_present_or_throw();

    std::vector<vix::cli::util::lockfile::LockedDependency> lockedDependencies;
    std::unordered_set<std::string> visited;

    for (const auto &dependency : manifestDependencies)
    {
      const auto spec = parse_dependency_spec(dependency);
      if (!spec.has_value())
      {
        throw std::runtime_error("invalid manifest dependency: " + dependency.id);
      }

      resolve_transitive_or_throw(*spec, visited, lockedDependencies);
    }

    return lockedDependencies;
  }
}
