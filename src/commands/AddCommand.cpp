#include <vix/cli/commands/AddCommand.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
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

    int run_cmd(const std::string &cmd)
    {
      const int rc = std::system(cmd.c_str());
      return rc == 0 ? 0 : 1;
    }

    struct PkgSpec
    {
      std::string ns;
      std::string name;
      std::string version;
    };

    bool parse_pkg_spec(const std::string &raw, PkgSpec &out)
    {
      // format: namespace/name@version
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

      // remove previous entry for same pkg
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
      if (fs::exists(registry_dir()))
        return 0;

      std::cerr << "vix: registry not synced. Run: vix registry sync\n";
      return 1;
    }

    int clone_checkout(const std::string &repoUrl,
                       const std::string &id,
                       const std::string &commit)
    {
      fs::create_directories(store_git_dir());

      const fs::path dst = store_git_dir() / id / commit;
      if (fs::exists(dst))
        return 0;

      fs::create_directories(dst.parent_path());

      {
        const std::string cmd = "git clone " + repoUrl + " " + dst.string();
        const int rc = run_cmd(cmd);
        if (rc != 0)
          return rc;
      }

      {
        const std::string cmd = "git -C " + dst.string() + " checkout " + commit;
        const int rc = run_cmd(cmd);
        if (rc != 0)
          return rc;
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

    PkgSpec spec;
    if (!parse_pkg_spec(args[0], spec))
    {
      std::cerr << "vix: invalid package spec. Expected: <namespace>/<name>@<version>\n";
      return 1;
    }

    const fs::path p = entry_path(spec.ns, spec.name);
    if (!fs::exists(p))
    {
      std::cerr << "vix: package not found in registry index: " << spec.ns << "/" << spec.name << "\n";
      std::cerr << "hint: run `vix registry sync` and ensure entry exists: " << p.string() << "\n";
      return 1;
    }

    try
    {
      const json entry = read_json_file_or_throw(p);

      const std::string repoUrl = entry.at("repo").at("url").get<std::string>();
      const json versions = entry.at("versions");

      if (!versions.contains(spec.version))
      {
        std::cerr << "vix: version not found: " << spec.version << "\n";
        return 1;
      }

      const json v = versions.at(spec.version);
      const std::string tag = v.at("tag").get<std::string>();
      const std::string commit = v.at("commit").get<std::string>();

      const std::string id = spec.ns + "." + spec.name;

      std::cout << "• resolve: " << spec.ns << "/" << spec.name << "@"
                << spec.version << " -> " << tag << " (" << commit << ")\n";

      int rc = clone_checkout(repoUrl, id, commit);
      if (rc != 0)
      {
        std::cerr << "✖ fetch failed\n";
        return rc;
      }

      write_lockfile_append(spec, repoUrl, commit, tag);

      std::cout << "✓ added: " << spec.ns << "/" << spec.name
                << " (pinned " << commit << ")\n";
      std::cout << "✓ lock:  " << lock_path().string() << "\n";
      std::cout << "✓ store: " << (store_git_dir() / id / commit).string() << "\n";

      return 0;
    }
    catch (const std::exception &ex)
    {
      std::cerr << "vix: add failed: " << ex.what() << "\n";
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
