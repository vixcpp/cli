/**
 *
 *  @file PublishCommand.cpp
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
#include <vix/cli/commands/PublishCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/util/Shell.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdio>
#include <chrono>
#include <iomanip>

#if defined(_WIN32)
#define vix_popen _popen
#define vix_pclose _pclose
#else
#define vix_popen popen
#define vix_pclose pclose
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  using namespace vix::cli::style;

  namespace
  {
    struct PublishOptions
    {
      std::string version;
      std::string notes;
      bool dryRun{false};
      bool cleanup{false};
    };

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

    static std::string lower_copy(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c)
                     { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    static std::string home_dir()
    {
#ifdef _WIN32
      const char *home = vix::utils::vix_getenv("USERPROFILE");
#else
      const char *home = vix::utils::vix_getenv("HOME");
#endif
      return home ? std::string(home) : std::string();
    }

    static fs::path vix_root()
    {
      const std::string h = home_dir();
      if (h.empty())
        return fs::path(".vix");
      return fs::path(h) / ".vix";
    }

    static fs::path registry_repo_dir()
    {
      // vix registry sync clones into ~/.vix/registry/index
      return vix_root() / "registry" / "index";
    }

    static fs::path registry_index_dir()
    {
      // entries folder inside the registry repo: ~/.vix/registry/index/index
      return registry_repo_dir() / "index";
    }

    static std::string iso_utc_now()
    {
      using namespace std::chrono;
      const auto now = system_clock::now();
      const std::time_t t = system_clock::to_time_t(now);

      std::tm tm{};
#if defined(_WIN32)
      gmtime_s(&tm, &t);
#else
      gmtime_r(&t, &tm);
#endif

      std::ostringstream oss;
      oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
      return oss.str();
    }

    static bool file_exists_nonempty(const fs::path &p)
    {
      std::error_code ec;
      return fs::exists(p, ec) && fs::is_regular_file(p, ec);
    }

    static json read_json_or_throw(const fs::path &p)
    {
      std::ifstream in(p);
      if (!in)
        throw std::runtime_error("cannot open: " + p.string());
      json j;
      in >> j;
      return j;
    }

    static void write_json_or_throw(const fs::path &p, const json &j)
    {
      std::ofstream out(p);
      if (!out)
        throw std::runtime_error("cannot write: " + p.string());
      out << j.dump(2) << "\n";
    }

    static int run_cmd_capture(const std::string &cmd, std::string &out)
    {
      out.clear();
      FILE *pipe = ::vix_popen(cmd.c_str(), "r");
      if (!pipe)
        return 127;

      char buffer[4096];
      while (true)
      {
        const size_t n = std::fread(buffer, 1, sizeof(buffer), pipe);
        if (n > 0)
          out.append(buffer, buffer + n);
        if (n < sizeof(buffer))
          break;
      }

      const int rc = ::vix_pclose(pipe);
      out = trim_copy(out);
      return rc;
    }

    static std::optional<std::string> git_top_level()
    {
      std::string out;
      const int rc = run_cmd_capture("git rev-parse --show-toplevel 2>/dev/null", out);
      if (rc != 0 || out.empty())
        return std::nullopt;
      return out;
    }

    static bool git_is_clean()
    {
      std::string out;
      const int rc = run_cmd_capture("git status --porcelain 2>/dev/null", out);
      if (rc != 0)
        return false;
      return out.empty();
    }

    static bool git_tag_exists(const std::string &tag)
    {
      // Accept annotated tags AND lightweight tags.
      // Annotated: tag^{tag} works
      // Lightweight: tag^{tag} fails, but refs/tags/<tag> exists.
      {
        const std::string cmd = "git rev-parse -q --verify " + tag + "^{tag} >/dev/null 2>&1";
        if (std::system(cmd.c_str()) == 0)
          return true;
      }
      {
        const std::string cmd = "git rev-parse -q --verify refs/tags/" + tag + " >/dev/null 2>&1";
        return std::system(cmd.c_str()) == 0;
      }
    }

    static std::optional<std::string> git_commit_for_tag(const std::string &tag)
    {
      std::string out;
      const std::string cmd = "git rev-list -n 1 " + tag + " 2>/dev/null";
      const int rc = run_cmd_capture(cmd, out);
      if (rc != 0 || out.empty())
        return std::nullopt;
      return out;
    }

    static std::optional<std::string> read_vix_json_namespace(const fs::path &repoRoot)
    {
      const fs::path p = repoRoot / "vix.json";
      if (!file_exists_nonempty(p))
        return std::nullopt;

      try
      {
        const json j = read_json_or_throw(p);
        if (j.is_object() && j.contains("namespace") && j["namespace"].is_string())
          return lower_copy(j["namespace"].get<std::string>());
      }
      catch (...)
      {
      }
      return std::nullopt;
    }

    static std::optional<std::string> read_vix_json_name(const fs::path &repoRoot)
    {
      const fs::path p = repoRoot / "vix.json";
      if (!file_exists_nonempty(p))
        return std::nullopt;

      try
      {
        const json j = read_json_or_throw(p);
        if (j.is_object() && j.contains("name") && j["name"].is_string())
          return lower_copy(j["name"].get<std::string>());
      }
      catch (...)
      {
      }
      return std::nullopt;
    }

    static std::optional<std::pair<std::string, std::string>> infer_from_git_remote()
    {
      std::string url;
      const int rc = run_cmd_capture("git remote get-url origin 2>/dev/null", url);
      if (rc != 0 || url.empty())
        return std::nullopt;

      std::string u = url;

      auto strip_suffix = [&](const std::string &suf)
      {
        if (u.size() >= suf.size() && u.rfind(suf) == u.size() - suf.size())
          u.erase(u.size() - suf.size());
      };
      strip_suffix(".git");

      std::string path;

      const auto posAt = u.find('@');
      const auto posColon = u.find(':');
      if (posAt != std::string::npos && posColon != std::string::npos && posColon > posAt)
      {
        path = u.substr(posColon + 1);
      }
      else
      {
        const auto pos = u.find("://");
        if (pos != std::string::npos)
        {
          std::string rest = u.substr(pos + 3);
          const auto slash = rest.find('/');
          if (slash != std::string::npos)
            path = rest.substr(slash + 1);
        }
      }

      if (path.empty())
        return std::nullopt;

      const auto slash = path.find('/');
      if (slash == std::string::npos)
        return std::nullopt;

      std::string ns = path.substr(0, slash);
      std::string name = path.substr(slash + 1);

      if (ns.empty() || name.empty())
        return std::nullopt;

      return std::make_pair(lower_copy(ns), lower_copy(name));
    }

    static void mark_version_published_or_throw(
        const fs::path &entryPath,
        json &entry,
        const std::string &version)
    {
      if (!entry.contains("versions") || !entry["versions"].is_object())
        entry["versions"] = json::object();

      if (!entry["versions"].contains(version) || !entry["versions"][version].is_object())
        entry["versions"][version] = json::object();

      entry["versions"][version]["status"] = "published";
      write_json_or_throw(entryPath, entry);
    }

    static PublishOptions parse_args_or_throw(const std::vector<std::string> &args)
    {
      PublishOptions opt;

      if (args.empty())
        throw std::runtime_error("missing version. Try: vix publish 0.2.0");

      opt.version = args[0];

      for (size_t i = 1; i < args.size(); ++i)
      {
        const std::string &a = args[i];

        if (a == "--dry-run")
        {
          opt.dryRun = true;
          continue;
        }

        if (a == "--notes")
        {
          if (i + 1 >= args.size())
            throw std::runtime_error("--notes requires a value");
          opt.notes = args[i + 1];
          ++i;
          continue;
        }

        if (a == "--cleanup")
        {
          opt.cleanup = true;
          continue;
        }

        const std::string prefix = "--notes=";
        if (a.rfind(prefix, 0) == 0)
        {
          opt.notes = a.substr(prefix.size());
          continue;
        }

        throw std::runtime_error("unknown flag: " + a);
      }

      opt.version = trim_copy(opt.version);
      if (opt.version.empty())
        throw std::runtime_error("version cannot be empty");

      return opt;
    }

    static bool is_git_repo(const fs::path &dir)
    {
      std::error_code ec;
      return fs::exists(dir / ".git", ec);
    }

    static bool command_exists(const std::string &name)
    {
#ifdef _WIN32
      const std::string cmd = "where " + name + " >nul 2>nul";
#else
      const std::string cmd = "command -v " + name + " >/dev/null 2>&1";
#endif
      return std::system(cmd.c_str()) == 0;
    }

    static std::string registry_file_name(const std::string &ns, const std::string &name)
    {
      return ns + "." + name + ".json";
    }

    static std::string branch_name(const std::string &ns, const std::string &name, const std::string &version)
    {
      std::string b = "publish-" + ns + "-" + name + "-" + version;
      for (char &c : b)
      {
        if (c == '/')
          c = '-';
      }
      return b;
    }

    static int publish_impl(const PublishOptions &opt)
    {
      vix::cli::util::section(std::cout, "Publish");

      const auto repoRootStr = git_top_level();
      if (!repoRootStr)
      {
        vix::cli::util::err_line(std::cerr, "not inside a git repository");
        vix::cli::util::warn_line(std::cerr, "Run this inside your library repo.");
        return 1;
      }

      const fs::path repoRoot = *repoRootStr;

      vix::cli::util::kv(std::cout, "repo", repoRoot.string());
      vix::cli::util::kv(std::cout, "version", opt.version);

      if (!git_is_clean())
      {
        vix::cli::util::err_line(std::cerr, "working tree is not clean");
        vix::cli::util::warn_line(std::cerr, "Commit your changes before publishing.");
        return 1;
      }

      const std::string tag = "v" + opt.version;
      vix::cli::util::kv(std::cout, "tag", tag);

      if (!git_tag_exists(tag))
      {
        vix::cli::util::err_line(std::cerr, "missing tag: " + tag);
        vix::cli::util::warn_line(std::cerr, "Create it then push it: git tag -a " + tag + " -m \"" + tag + "\" && git push --tags");
        return 1;
      }

      const auto commitOpt = git_commit_for_tag(tag);
      if (!commitOpt)
      {
        vix::cli::util::err_line(std::cerr, "failed to resolve commit for tag: " + tag);
        return 1;
      }
      const std::string commit = *commitOpt;
      vix::cli::util::kv(std::cout, "commit", commit);

      std::optional<std::string> ns = read_vix_json_namespace(repoRoot);
      std::optional<std::string> name = read_vix_json_name(repoRoot);

      if (!ns || !name)
      {
        const auto inferred = infer_from_git_remote();
        if (inferred)
        {
          if (!ns)
            ns = inferred->first;
          if (!name)
            name = inferred->second;
        }
      }

      if (!ns || !name || ns->empty() || name->empty())
      {
        vix::cli::util::err_line(std::cerr, "cannot infer package namespace/name");
        vix::cli::util::warn_line(std::cerr, "Fix: add { \"namespace\": \"...\", \"name\": \"...\" } in vix.json or ensure git remote origin is set.");
        return 1;
      }

      const std::string pkgId = *ns + "/" + *name;
      vix::cli::util::kv(std::cout, "id", pkgId);

      const fs::path regRepo = registry_repo_dir();
      const fs::path regIndex = registry_index_dir();

      vix::cli::util::kv(std::cout, "registry", regRepo.string());

      if (!fs::exists(regRepo) || !is_git_repo(regRepo) || !fs::exists(regIndex))
      {
        vix::cli::util::err_line(std::cerr, "registry is not available locally: " + regRepo.string());
        vix::cli::util::warn_line(std::cerr, "Run: vix registry sync");
        return 1;
      }

      const fs::path entryPath = regIndex / registry_file_name(*ns, *name);
      vix::cli::util::kv(std::cout, "entry", entryPath.string());

      json entry;

      if (fs::exists(entryPath))
      {
        try
        {
          entry = read_json_or_throw(entryPath);
        }
        catch (const std::exception &ex)
        {
          vix::cli::util::err_line(std::cerr, std::string("failed to read registry entry: ") + ex.what());
          return 1;
        }
      }
      else
      {
        std::string remoteUrl;
        run_cmd_capture("git remote get-url origin 2>/dev/null", remoteUrl);
        remoteUrl = trim_copy(remoteUrl);

        std::string httpsUrl = remoteUrl;
        if (!httpsUrl.empty())
        {
          if (httpsUrl.find("git@") == 0)
          {
            const auto pos = httpsUrl.find(':');
            if (pos != std::string::npos)
            {
              const std::string path = httpsUrl.substr(pos + 1);
              httpsUrl = "https://github.com/" + path;
              if (httpsUrl.size() >= 4 && httpsUrl.rfind(".git") == httpsUrl.size() - 4)
                httpsUrl.erase(httpsUrl.size() - 4);
            }
          }
          if (httpsUrl.size() >= 4 && httpsUrl.rfind(".git") == httpsUrl.size() - 4)
            httpsUrl.erase(httpsUrl.size() - 4);
        }

        entry = json::object();
        entry["name"] = *name;
        entry["namespace"] = *ns;
        entry["displayName"] = *name;
        entry["description"] = "";
        entry["keywords"] = json::array();
        entry["license"] = "MIT";
        entry["repo"] = json::object({{"url", httpsUrl}, {"defaultBranch", "main"}});
        entry["type"] = "header-only";
        entry["manifestPath"] = "vix.json";
        entry["homepage"] = httpsUrl;
        entry["versions"] = json::object();
      }

      if (!entry.is_object())
      {
        vix::cli::util::err_line(std::cerr, "invalid registry entry format");
        return 1;
      }

      if (!entry.contains("versions") || !entry["versions"].is_object())
        entry["versions"] = json::object();

      if (entry["versions"].contains(opt.version))
      {
        const json &existing = entry["versions"][opt.version];
        const std::string status = (existing.contains("status") && existing["status"].is_string())
                                       ? existing["status"].get<std::string>()
                                       : std::string();

        if (status == "published")
        {
          vix::cli::util::err_line(std::cerr, "version already published in registry: " + opt.version);
          return 1;
        }

        // pending or unknown -> resume
        vix::cli::util::warn_line(std::cout, "found existing pending entry, resuming publish for: " + opt.version);
      }

      json v = json::object();
      v["tag"] = tag;
      v["commit"] = commit;
      v["publishedAt"] = iso_utc_now();
      v["status"] = "pending";
      if (!opt.notes.empty())
        v["notes"] = opt.notes;

      entry["versions"][opt.version] = v;
      if (opt.dryRun)
      {
        vix::cli::util::ok_line(std::cout, "dry-run: would update: " + entryPath.string());
        std::cout << "\n"
                  << entry.dump(2) << "\n";
        return 0;
      }

      std::error_code ec;
      fs::create_directories(entryPath.parent_path(), ec);

      try
      {
        write_json_or_throw(entryPath, entry);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, std::string("failed to write registry entry: ") + ex.what());
        return 1;
      }

      const std::string branch = branch_name(*ns, *name, opt.version);
      vix::cli::util::kv(std::cout, "branch", branch);

      {
        const std::string cmd =
            "git -C " + regRepo.string() + " pull -q --ff-only";
        const int rc = vix::cli::util::run_cmd_retry_debug(cmd);
        if (rc != 0)
        {
          vix::cli::util::err_line(std::cerr, "failed to update local registry repo (pull --ff-only)");
          return rc;
        }
      }

      {
        const std::string cmd =
            "git -C " + regRepo.string() + " checkout -B " + branch + " -q";
        const int rc = vix::cli::util::run_cmd_retry_debug(cmd);
        if (rc != 0)
        {
          vix::cli::util::err_line(std::cerr, "failed to create branch: " + branch);
          return rc;
        }
      }

      {
        const std::string relEntry = (fs::path("index") / registry_file_name(*ns, *name)).generic_string();
        const std::string cmd = "git -C " + regRepo.string() + " add " + relEntry;

        const int rc = vix::cli::util::run_cmd_retry_debug(cmd);
        if (rc != 0)
          return rc;
      }

      {
        const std::string msg = "registry: " + pkgId + " v" + opt.version;
        const std::string cmd =
            "git -C " + regRepo.string() + " commit -q -m \"" + msg + "\"";
        const int rc = vix::cli::util::run_cmd_retry_debug(cmd);
        if (rc != 0)
        {
          vix::cli::util::err_line(std::cerr, "failed to commit registry update");
          return rc;
        }
      }

      {
        const std::string pushCmd =
            "git -C " + regRepo.string() + " push -u origin " + branch;

        const int rc = vix::cli::util::run_cmd_retry_debug(pushCmd);
        if (rc != 0)
        {
          vix::cli::util::err_line(std::cerr, "failed to push branch to origin");
          return rc;
        }

        if (opt.cleanup)
        {
          const std::string cleanupCmd =
              "git -C " + regRepo.string() +
              " branch --format='%(refname:short)' "
              "| grep '^publish-" +
              *ns + "-" + *name + "-' "
                                  "| grep -v '^" +
              branch + "$' "
                       "| xargs -r -n1 git -C " +
              regRepo.string() + " branch -D";

          (void)vix::cli::util::run_cmd_retry_debug(cleanupCmd);
        }
      }

      // Mark as published locally after successful push.
      try
      {
        entry = read_json_or_throw(entryPath);
        mark_version_published_or_throw(entryPath, entry, opt.version);

        const std::string relEntry =
            (fs::path("index") / registry_file_name(*ns, *name)).generic_string();

        (void)vix::cli::util::run_cmd_retry_debug("git -C " + regRepo.string() + " add " + relEntry);
        (void)vix::cli::util::run_cmd_retry_debug(
            "git -C " + regRepo.string() +
            " commit -q -m \"registry: mark published " + pkgId + " v" + opt.version + "\"");
        (void)vix::cli::util::run_cmd_retry_debug("git -C " + regRepo.string() + " push");
      }
      catch (...)
      {
        // Not fatal: publish already pushed, only status update failed.
        vix::cli::util::warn_line(std::cout, "warning: could not mark version as published locally");
      }

      bool prCreated = false;

      if (command_exists("gh"))
      {
        bool ghAuthed = false;
        {
          std::string out;
          const int rc = run_cmd_capture("gh auth status -h github.com 2>/dev/null", out);
          ghAuthed = (rc == 0);
        }

        if (ghAuthed)
        {
          const std::string title = "registry: " + pkgId + " v" + opt.version;
          std::string body;
          body += "Adds " + pkgId + " v" + opt.version + " to the Vix registry.\n\n";
          body += "- tag: " + tag + "\n";
          body += "- commit: " + commit + "\n";

          std::string cmd =
              "gh pr create "
              "--repo vixcpp/registry "
              "--base main "
              "--head " +
              branch + " "
                       "--title \"" +
              title + "\" "
                      "--body \"" +
              body + "\"";

          const int rc = vix::cli::util::run_cmd_retry_debug(cmd);
          if (rc == 0)
            prCreated = true;
        }
      }

      if (prCreated)
      {
        vix::cli::util::ok_line(std::cout, "PR created for: " + pkgId + " v" + opt.version);
        return 0;
      }

      vix::cli::util::ok_line(std::cout, "branch pushed: " + branch);
      vix::cli::util::warn_line(std::cout, "Create a PR on GitHub: vixcpp/registry ← " + branch);
      vix::cli::util::warn_line(std::cout, "Tip: install/auth gh to auto-create PR next time.");
      return 0;
    }

  }

  int PublishCommand::run(const std::vector<std::string> &args)
  {
    try
    {
      const PublishOptions opt = parse_args_or_throw(args);
      return publish_impl(opt);
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(std::cerr, std::string("publish failed: ") + ex.what());
      return 1;
    }
  }

  int PublishCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix publish <version> [--notes \"...\"] [--dry-run]\n\n"
        << "Description:\n"
        << "  Publish a C++ package version into the Vix Registry by updating the local registry clone\n"
        << "  (~/.vix/registry/index), creating a branch, committing, pushing, and opening a PR (if gh is available).\n\n"
        << "Requirements:\n"
        << "  - Run inside your library git repo\n"
        << "  - Tag must exist: v<version>\n"
        << "  - Registry must be synced: vix registry sync\n\n"
        << "Examples:\n"
        << "  vix publish 0.2.0\n"
        << "  vix publish 0.2.0 --notes \"Add count_leaves helpers\"\n"
        << "  vix publish 0.2.0 --dry-run\n";
    return 0;
  }
}
