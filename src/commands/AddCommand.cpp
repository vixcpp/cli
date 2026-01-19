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

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
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
      std::string version;
    };

    static std::string to_lower(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c)
                     { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    bool parse_pkg_spec(const std::string &raw, PkgSpec &out)
    {
      const auto slash = raw.find('/');
      if (slash == std::string::npos)
        return false;

      const auto at = raw.find('@');
      if (at == std::string::npos)
        return false;

      out.ns = raw.substr(0, slash);
      out.name = raw.substr(slash + 1, at - (slash + 1));
      out.version = raw.substr(at + 1);

      if (out.ns.empty() || out.name.empty() || out.version.empty())
        return false;

      return true;
    }

    json read_json_file_or_throw(const fs::path &p)
    {
      std::ifstream in(p);
      if (!in)
        throw std::runtime_error("cannot open file: " + p.string());

      json j;
      in >> j;
      return j;
    }

    fs::path entry_path(const std::string &ns, const std::string &name)
    {
      return registry_index_dir() / (ns + "." + name + ".json");
    }

    fs::path lock_path()
    {
      return fs::current_path() / "vix.lock";
    }

    void write_lockfile_append(
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
      for (const auto &d : deps)
      {
        const std::string id = d.value("id", "");
        if (id != (spec.ns + "/" + spec.name))
          filtered.push_back(d);
      }
      deps = filtered;

      json dep;
      dep["id"] = spec.ns + "/" + spec.name;
      dep["version"] = spec.version;
      dep["repo"] = repoUrl;
      dep["tag"] = tag;
      dep["commit"] = commitSha;

      deps.push_back(dep);

      std::ofstream out(lp);
      out << lock.dump(2) << "\n";
    }

    int ensure_registry_present()
    {
      if (fs::exists(registry_dir()) && fs::exists(registry_index_dir()))
        return 0;

      vix::cli::util::err_line(std::cerr, "registry not synced");
      vix::cli::util::warn_line(std::cerr, "Run: vix registry sync");
      return 1;
    }

    int clone_checkout(
        const std::string &repoUrl,
        const std::string &id,
        const std::string &commit)
    {
      fs::create_directories(store_git_dir());

      const fs::path dst = store_git_dir() / id / commit;
      if (fs::exists(dst))
        return 0;

      fs::create_directories(dst.parent_path());

      {
        const std::string cmd =
            "git clone -q --depth 1 " + repoUrl + " " + dst.string();
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

      const std::string q = to_lower(query_raw);

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

          const std::string id = entry.value("id", "");
          const std::string desc = entry.value("description", "");
          const std::string repo = entry.contains("repo") ? entry["repo"].value("url", "") : "";

          std::string latest;
          if (entry.contains("latest"))
            latest = entry.value("latest", "");

          const std::string hay = to_lower(id + " " + desc + " " + repo + " " + p.filename().string());

          if (q.empty() || hay.find(q) != std::string::npos)
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

      std::sort(hits.begin(), hits.end(), [](const SearchHit &a, const SearchHit &b)
                { return a.id < b.id; });

      return hits;
    }

    static void print_search_hits(const std::vector<SearchHit> &hits)
    {
      if (hits.empty())
      {
        vix::cli::util::warn_line(std::cout, "No matches found in the local registry index.");
        return;
      }

      for (const auto &h : hits)
      {
        vix::cli::util::pkg_line(std::cout, h.id, h.latest, h.description, h.repo);
        std::cout << "\n";
      }
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
          if (v > best)
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
      vix::cli::util::warn_line(std::cerr, "Expected: <namespace>/<name>@<version>");
      vix::cli::util::warn_line(std::cerr, "Example:  vix add gaspardkirira/tree@0.1.0");
      vix::cli::util::warn_line(std::cerr, std::string("Try search: vix search ") + raw);
      return 1;
    }

    const fs::path p = entry_path(spec.ns, spec.name);
    if (!fs::exists(p))
    {
      vix::cli::util::err_line(std::cerr, "package not found: " + spec.ns + "/" + spec.name);

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

      const std::string repoUrl = entry.at("repo").at("url").get<std::string>();
      const json versions = entry.at("versions");

      if (!versions.contains(spec.version))
      {
        vix::cli::util::err_line(std::cerr, "version not found: " + spec.version);

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
          vix::cli::util::warn_line(std::cerr, "Try: vix add " + spec.ns + "/" + spec.name + "@" + latest);

        return 1;
      }

      const json v = versions.at(spec.version);
      const std::string tag = v.at("tag").get<std::string>();
      const std::string commit = v.at("commit").get<std::string>();
      const std::string id = spec.ns + "." + spec.name;

      vix::cli::util::section(std::cout, "Add");
      vix::cli::util::kv(std::cout, "id", spec.ns + "/" + spec.name);
      vix::cli::util::kv(std::cout, "version", spec.version);
      vix::cli::util::kv(std::cout, "tag", tag);
      vix::cli::util::kv(std::cout, "commit", commit);

      step("fetching sources...");

      const int rc = clone_checkout(repoUrl, id, commit);
      if (rc != 0)
      {
        vix::cli::util::err_line(std::cerr, "fetch failed");
        vix::cli::util::warn_line(std::cerr, "Check your git access and network, then retry.");
        return rc;
      }

      write_lockfile_append(spec, repoUrl, commit, tag);

      vix::cli::util::ok_line(std::cout, "added: " + spec.ns + "/" + spec.name + " (pinned " + commit + ")");
      vix::cli::util::ok_line(std::cout, "lock:  " + lock_path().string());
      vix::cli::util::ok_line(std::cout, "store: " + (store_git_dir() / id / commit).string());

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
        << "  vix add <namespace>/<name>@<version>\n\n"
        << "Example:\n"
        << "  vix registry sync\n"
        << "  vix add gaspardkirira/tree@0.1.0\n\n"
        << "Notes:\n"
        << "  - V1 requires an exact version.\n"
        << "  - The lockfile pins the resolved commit SHA.\n";
    return 0;
  }
}
