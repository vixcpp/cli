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
#include <vix/cli/util/Shell.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <optional>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  using namespace vix::cli::style;

  namespace
  {
    static std::string find_latest_version(const json &entry);
    static std::vector<std::string> list_versions(const json &entry);

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

    struct PkgSpec
    {
      std::string ns;
      std::string name;

      std::string requestedVersion;
      std::string resolvedVersion;

      std::string id() const { return ns + "/" + name; }
    };

    static std::string to_lower(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c)
                     { return static_cast<char>(std::tolower(c)); });
      return s;
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

    static bool parse_pkg_spec(const std::string &raw, PkgSpec &out)
    {
      const auto slash = raw.find('/');
      if (slash == std::string::npos)
        return false;

      const auto at = raw.find('@');

      out.ns = trim_copy(raw.substr(0, slash));

      if (at == std::string::npos)
      {
        out.name = trim_copy(raw.substr(slash + 1));
        out.requestedVersion.clear();
      }
      else
      {
        out.name = trim_copy(raw.substr(slash + 1, at - (slash + 1)));
        out.requestedVersion = trim_copy(raw.substr(at + 1));
      }

      out.resolvedVersion.clear();

      if (out.ns.empty() || out.name.empty())
        return false;

      // Si @ est présent, la version doit etre non vide
      if (at != std::string::npos && out.requestedVersion.empty())
        return false;

      return true;
    }

    static int resolve_version_v1(const json &entry, PkgSpec &spec)
    {
      if (!spec.requestedVersion.empty())
      {
        spec.resolvedVersion = spec.requestedVersion;
        return 0;
      }

      const std::string latest = find_latest_version(entry);
      if (latest.empty())
      {
        vix::cli::util::err_line(std::cerr,
                                 "no versions available for: " + spec.ns + "/" + spec.name);
        return 1;
      }

      spec.resolvedVersion = latest;
      return 0;
    }

    static json read_json_file_or_throw(const fs::path &p)
    {
      std::ifstream in(p);
      if (!in)
        throw std::runtime_error("cannot open file: " + p.string());

      json j;
      in >> j;
      return j;
    }

    static fs::path entry_path(const std::string &ns, const std::string &name)
    {
      return registry_index_dir() / (ns + "." + name + ".json");
    }

    static fs::path lock_path()
    {
      return fs::current_path() / "vix.lock";
    }

    static void write_lockfile_append(
        const PkgSpec &spec,
        const std::string &repoUrl,
        const std::string &commitSha,
        const std::string &tag)
    {
      json lock;

      const fs::path lp = lock_path();
      if (fs::exists(lp))
        lock = read_json_file_or_throw(lp);
      else
        lock = json::object();

      if (!lock.contains("lockVersion"))
        lock["lockVersion"] = 1;

      if (!lock.contains("dependencies"))
        lock["dependencies"] = json::array();

      auto &deps = lock["dependencies"];
      json filtered = json::array();

      const std::string wantedId = spec.ns + "/" + spec.name;

      for (const auto &d : deps)
      {
        const std::string id = d.value("id", "");
        if (id != wantedId)
          filtered.push_back(d);
      }
      deps = filtered;

      json dep;
      dep["id"] = wantedId;
      dep["version"] = spec.resolvedVersion; // IMPORTANT
      dep["repo"] = repoUrl;
      dep["tag"] = tag;
      dep["commit"] = commitSha;

      deps.push_back(dep);

      std::ofstream out(lp);
      out << lock.dump(2) << "\n";
    }

    static int ensure_registry_present()
    {
      if (fs::exists(registry_dir()) && fs::exists(registry_index_dir()))
        return 0;

      vix::cli::util::err_line(std::cerr, "registry not synced");
      vix::cli::util::warn_line(std::cerr, "Run: vix registry sync");
      return 1;
    }

    static bool contains_any_icase(std::string hay, const std::string &needleLower)
    {
      if (needleLower.empty())
        return true;
      hay = to_lower(hay);
      return hay.find(needleLower) != std::string::npos;
    }

    struct SearchHit
    {
      std::string id;
      std::string latest;
      std::string description;
      std::string repo;
    };

    static std::vector<SearchHit> search_registry_local(const std::string &query_raw)
    {
      std::vector<SearchHit> hits;

      const fs::path dir = registry_index_dir();
      if (!fs::exists(dir))
        return hits;

      const std::string q = to_lower(trim_copy(query_raw));

      for (const auto &it : fs::directory_iterator(dir))
      {
        if (!it.is_regular_file())
          continue;

        const fs::path p = it.path();
        if (p.extension() != ".json")
          continue;

        try
        {
          const json entry = read_json_file_or_throw(p);

          const std::string ns = entry.value("namespace", "");
          const std::string name = entry.value("name", "");
          const std::string id = (ns.empty() || name.empty()) ? "" : (ns + "/" + name);

          const std::string desc = entry.value("description", "");
          const std::string repo = (entry.contains("repo") && entry["repo"].is_object())
                                       ? entry["repo"].value("url", "")
                                       : "";

          std::string latest;
          if (entry.contains("latest") && entry["latest"].is_string())
            latest = entry["latest"].get<std::string>();

          const std::string hay =
              id + " " + desc + " " + repo + " " + p.filename().string();

          if (contains_any_icase(hay, q))
          {
            SearchHit h;
            h.id = id;
            h.latest = latest;
            h.description = desc;
            h.repo = repo;
            hits.push_back(std::move(h));
          }
        }
        catch (...)
        {
        }
      }

      hits.erase(std::remove_if(hits.begin(), hits.end(),
                                [](const SearchHit &h)
                                { return h.id.empty(); }),
                 hits.end());

      std::sort(hits.begin(), hits.end(), [](const SearchHit &a, const SearchHit &b)
                { return a.id < b.id; });

      return hits;
    }

    static void print_search_hits(const std::vector<SearchHit> &hits, std::size_t limit = 15)
    {
      if (hits.empty())
      {
        vix::cli::util::warn_line(std::cout, "No matches found in the local registry index.");
        return;
      }

      const std::size_t n = std::min<std::size_t>(hits.size(), limit);

      for (std::size_t i = 0; i < n; ++i)
      {
        const auto &h = hits[i];
        vix::cli::util::pkg_line(std::cout, h.id, h.latest, h.description, h.repo);
        std::cout << "\n";
      }

      if (hits.size() > limit)
        vix::cli::util::ok_line(std::cout, "Showing " + std::to_string(n) + " of " + std::to_string(hits.size()) + " result(s).");
      else
        vix::cli::util::ok_line(std::cout, "Found " + std::to_string(hits.size()) + " result(s).");
    }

    static std::string find_latest_version(const json &entry)
    {
      if (entry.contains("latest") && entry["latest"].is_string())
        return entry["latest"].get<std::string>();

      if (entry.contains("versions") && entry["versions"].is_object())
      {
        std::string best;
        for (auto it = entry["versions"].begin(); it != entry["versions"].end(); ++it)
        {
          const std::string v = it.key();
          if (best.empty() || v > best)
            best = v;
        }
        return best;
      }
      return {};
    }

    static std::vector<std::string> list_versions(const json &entry)
    {
      std::vector<std::string> out;
      if (!entry.contains("versions") || !entry["versions"].is_object())
        return out;

      for (auto it = entry["versions"].begin(); it != entry["versions"].end(); ++it)
        out.push_back(it.key());

      std::sort(out.begin(), out.end());
      return out;
    }

    static int clone_checkout(
        const std::string &repoUrl,
        const std::string &idDot,
        const std::string &commit,
        std::string &outDir)
    {
      fs::create_directories(store_git_dir());

      const fs::path dst = store_git_dir() / idDot / commit;
      outDir = dst.string();

      if (fs::exists(dst))
        return 0;

      fs::create_directories(dst.parent_path());

      {
        const std::string cmd =
            "git clone -q " + repoUrl + " " + dst.string();
        const int rc = vix::cli::util::run_cmd_retry_debug(cmd);
        if (rc != 0)
          return rc;
      }

      {
        const std::string cmd =
            "git -C " + dst.string() +
            " -c advice.detachedHead=false checkout -q " + commit;
        const int rc = vix::cli::util::run_cmd_retry_debug(cmd);
        if (rc != 0)
          return rc;
      }

      return 0;
    }

    static void print_commit_missing_help(
        const std::string &pkgId,
        const std::string &repoUrl,
        const std::string &tag,
        const std::string &commit)
    {
      vix::cli::util::warn_line(std::cerr, "The registry entry points to a commit that is not reachable in the remote repository.");
      vix::cli::util::warn_line(std::cerr, "This usually happens when the tag/commit was not pushed, or history was rewritten (force-push/rebase).");
      std::cerr << "\n";
      vix::cli::util::kv(std::cout, "package", pkgId);
      vix::cli::util::kv(std::cout, "repo", repoUrl);
      vix::cli::util::kv(std::cout, "tag", tag);
      vix::cli::util::kv(std::cout, "commit", commit);
      std::cout << "\n";
      vix::cli::util::warn_line(std::cerr, "Fix:");
      vix::cli::util::warn_line(std::cerr, "  - Ensure the tag exists on origin: git push --tags");
      vix::cli::util::warn_line(std::cerr, "  - Or publish a new version with a valid tag/commit (recommended).");
    }

    static std::optional<PkgSpec> parse_dep_obj_v1(const json &d)
    {
      if (!d.is_object())
        return std::nullopt;

      const std::string id = d.value("id", "");
      const std::string ver = d.value("version", "");

      const auto slash = id.find('/');
      if (slash == std::string::npos)
        return std::nullopt;

      PkgSpec s;
      s.ns = trim_copy(id.substr(0, slash));
      s.name = trim_copy(id.substr(slash + 1));

      s.requestedVersion = trim_copy(ver); // ici
      s.resolvedVersion.clear();

      if (s.ns.empty() || s.name.empty() || s.requestedVersion.empty())
        return std::nullopt;

      return s;
    }

    static std::vector<PkgSpec> read_vix_json_deps_v1(const fs::path &repoDir)
    {
      std::vector<PkgSpec> out;

      const fs::path p = repoDir / "vix.json";
      if (!fs::exists(p))
        return out;

      json j;
      try
      {
        j = read_json_file_or_throw(p);
      }
      catch (...)
      {
        return out;
      }

      if (!j.contains("deps") || !j["deps"].is_array())
        return out;

      for (const auto &d : j["deps"])
      {
        auto spec = parse_dep_obj_v1(d);
        if (spec)
          out.push_back(*spec);
      }

      return out;
    }

    static int ensure_install_one_v1(
        PkgSpec &spec,
        std::string &outInstalledDir,
        std::string &outRepoUrl,
        std::string &outCommit,
        std::string &outTag)
    {
      const fs::path p = entry_path(spec.ns, spec.name);
      if (!fs::exists(p))
      {
        vix::cli::util::err_line(std::cerr, "package not found: " + spec.id());
        return 1;
      }

      const json entry = read_json_file_or_throw(p);

      const int vr = resolve_version_v1(entry, spec);
      if (vr != 0)
        return vr;

      const std::string repoUrl = entry.at("repo").at("url").get<std::string>();
      const json versions = entry.at("versions");

      if (!versions.contains(spec.resolvedVersion))
      {
        vix::cli::util::err_line(std::cerr,
                                 "version not found: " + spec.id() + "@" + spec.resolvedVersion);
        return 1;
      }

      const json v = versions.at(spec.resolvedVersion);
      const std::string tag = v.at("tag").get<std::string>();
      const std::string commit = v.at("commit").get<std::string>();

      const std::string idDot = spec.ns + "." + spec.name;

      std::string outDir;
      const int rc = clone_checkout(repoUrl, idDot, commit, outDir);
      if (rc != 0)
        return rc;

      outInstalledDir = outDir;
      outRepoUrl = repoUrl;
      outCommit = commit;
      outTag = tag;

      // lockfile = version exacte résolue + commit
      write_lockfile_append(spec, repoUrl, commit, tag);

      return 0;
    }

    static int install_transitive_v1(
        PkgSpec root,
        std::unordered_set<std::string> &visited)
    {
      // on installe root (resolve inside ensure_install_one_v1)
      std::string dir, repo, commit, tag;
      const int rc = ensure_install_one_v1(root, dir, repo, commit, tag);
      if (rc != 0)
        return rc;

      const std::string key = root.ns + "/" + root.name + "@" + root.resolvedVersion;
      if (visited.count(key))
        return 0;
      visited.insert(key);

      const auto deps = read_vix_json_deps_v1(fs::path(dir));
      for (auto d : deps)
      {
        const int rc2 = install_transitive_v1(d, visited);
        if (rc2 != 0)
          return rc2;
      }

      return 0;
    }
  }

  int AddCommand::run(const std::vector<std::string> &args)
  {
    if (args.empty())
      return help();

    if (ensure_registry_present() != 0)
      return 1;

    const std::string raw = args[0];

    PkgSpec spec;
    if (!parse_pkg_spec(raw, spec))
    {
      vix::cli::util::err_line(std::cerr, "invalid package spec");
      vix::cli::util::warn_line(std::cerr, "Expected: <namespace>/<name>[@<version>]");
      vix::cli::util::warn_line(std::cerr, "Example:  vix add gaspardkirira/tree");
      vix::cli::util::warn_line(std::cerr, "Example:  vix add gaspardkirira/tree@0.1.0");
      vix::cli::util::warn_line(std::cerr, std::string("Try search: vix search ") + raw);
      return 1;
    }

    const fs::path p = entry_path(spec.ns, spec.name);
    if (!fs::exists(p))
    {
      vix::cli::util::err_line(std::cerr, "package not found: " + spec.id());

      vix::cli::util::section(std::cout, "Search");
      vix::cli::util::kv(std::cout, "query", vix::cli::util::quote(spec.name));

      const auto hits = search_registry_local(spec.name);
      print_search_hits(hits);

      vix::cli::util::warn_line(std::cerr, "If you just updated the registry, run: vix registry sync");
      return 1;
    }

    try
    {
      const json entry = read_json_file_or_throw(p);

      // resolve version (latest if omitted)
      const int vr = resolve_version_v1(entry, spec);
      if (vr != 0)
        return vr;

      if (spec.requestedVersion.empty())
      {
        vix::cli::util::ok_line(std::cout,
                                "resolved: " + spec.ns + "/" + spec.name + "@" + spec.resolvedVersion);
      }

      const json versions = entry.at("versions");
      if (!versions.contains(spec.resolvedVersion))
      {
        vix::cli::util::err_line(std::cerr,
                                 "version not found: " + spec.id() + "@" + spec.resolvedVersion);

        const std::string latest = find_latest_version(entry);
        const auto all = list_versions(entry);

        if (!all.empty())
        {
          vix::cli::util::section(std::cout, "Available versions");
          for (const auto &v : all)
            std::cout << "  " << GRAY << "• " << RESET << BOLD << v << RESET << "\n";
          std::cout << "\n";
        }

        if (!latest.empty())
          vix::cli::util::warn_line(std::cerr, "Try: vix add " + spec.id() + "@" + latest);

        return 1;
      }

      const json v = versions.at(spec.resolvedVersion);
      const std::string tag = v.at("tag").get<std::string>();
      const std::string commit = v.at("commit").get<std::string>();

      const std::string repoUrl = entry.at("repo").at("url").get<std::string>();
      const std::string pkgId = spec.id();

      vix::cli::util::section(std::cout, "Add");
      vix::cli::util::kv(std::cout, "id", pkgId);
      vix::cli::util::kv(std::cout, "version", spec.resolvedVersion);
      vix::cli::util::kv(std::cout, "tag", tag);
      vix::cli::util::kv(std::cout, "commit", commit);

      step("installing dependencies (transitive)...");

      std::unordered_set<std::string> visited;
      const int rc = install_transitive_v1(spec, visited);
      if (rc != 0)
        return rc;

      vix::cli::util::ok_line(std::cout, "added: " + pkgId + "@" + spec.resolvedVersion);
      vix::cli::util::ok_line(std::cout, "lock:  " + lock_path().string());
      vix::cli::util::ok_line(std::cout, "deps:  " + std::to_string(visited.size()));
      return 0;
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(std::cerr, std::string("add failed: ") + ex.what());
      return 1;
    }
  }

  int AddCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix add <namespace>/<name>[@<version>]\n\n"

        << "Description:\n"
        << "  Install a package from the Vix Registry.\n"
        << "  If @version is omitted, the latest version is resolved automatically.\n\n"

        << "Examples:\n"
        << "  vix registry sync\n"
        << "  vix add gaspardkirira/tree\n"
        << "  vix add gaspardkirira/tree@0.1.0\n\n"

        << "Behavior:\n"
        << "  - Resolves the requested version (or latest if omitted)\n"
        << "  - Clones the repository at the exact commit\n"
        << "  - Installs transitive dependencies\n"
        << "  - Writes a vix.lock file pinning the resolved commit SHA\n\n"

        << "Notes:\n"
        << "  - Run 'vix registry sync' if a package cannot be found.\n"
        << "  - The lockfile guarantees deterministic builds.\n";

    return 0;
  }
}
