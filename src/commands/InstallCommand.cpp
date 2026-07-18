/**
 *
 *  @file InstallCommand.cpp
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
#include <vix/cli/commands/InstallCommand.hpp>
#include <vix/cli/commands/RegistryCommand.hpp>
#include <vix/cli/commands/run/detail/RunnableExecutableResolver.hpp>
#include <vix/cli/app/AppManifest.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/util/Shell.hpp>
#include <vix/cli/util/Hash.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/commands/helpers/ProcessHelpers.hpp>
#include <vix/utils/Env.hpp>
#include <vix/cli/util/Semver.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>

namespace fs = std::filesystem;
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using json = nlohmann::json;

namespace vix::commands
{
  using namespace vix::cli::style;

  namespace
  {
    struct ParsedArgs
    {
      bool globalMode{false};
      std::string globalSpec;

      std::string gitSpec;
      std::string gitName;
      std::string gitTag;
      std::string gitBranch;
      std::string gitRev;
      std::string gitTarget;
      std::string gitSubdirectory;
      std::string gitInclude;
      bool gitHeaderOnly{false};
      bool allowPrerelease{false};
      bool yes{false};
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

    static void split_compact_git_revision(struct ParsedArgs &parsed);

    struct DepResolved
    {
      std::string id;
      std::string version;
      std::string repo;
      std::string tag;
      std::string commit;
      std::string hash;
      std::string hashAlgorithm;
      int hashVersion{0};
      std::string source{"registry"};
      std::string requested;
      std::string subdirectory;
      bool headerOnly{false};
      std::vector<std::string> includes;
      std::vector<std::pair<std::string, std::string>> cmakeOptions;

      std::string type;
      std::string include{"include"};
      std::vector<std::string> dependencies;
      std::vector<std::string> cmakeTargets;
      json extensions;

      fs::path checkout;
      fs::path linkDir;
    };

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

    static fs::path registry_dir()
    {
      return vix_root() / "registry" / "index";
    }

    static fs::path registry_index_dir()
    {
      return registry_dir() / "index";
    }

    static fs::path store_git_dir()
    {
      return vix_root() / "store" / "git";
    }

    static fs::path git_cache_dir()
    {
      return vix_root() / "cache" / "git";
    }

    static fs::path lock_path()
    {
      return fs::current_path() / "vix.lock";
    }

    static fs::path project_vix_dir()
    {
      return fs::current_path() / ".vix";
    }

    static fs::path project_deps_dir()
    {
      return project_vix_dir() / "deps";
    }

    static fs::path project_deps_cmake()
    {
      return project_vix_dir() / "vix_deps.cmake";
    }

    static fs::path global_root_dir()
    {
      if (const char *p = vix::utils::vix_getenv("VIX_GLOBAL_PREFIX"); p && *p)
        return fs::path(p);

      return vix_root() / "global";
    }

    static fs::path global_pkgs_dir()
    {
      return global_root_dir() / "packages";
    }

    static fs::path global_manifest_path()
    {
      return global_root_dir() / "installed.json";
    }

    static fs::path global_bin_dir()
    {
      return global_root_dir() / "bin";
    }

    static fs::path global_build_dir()
    {
      return global_root_dir() / "build";
    }

    static fs::path global_tmp_dir()
    {
      return global_root_dir() / "tmp";
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

    static std::string format_elapsed(std::chrono::steady_clock::duration d)
    {
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
      if (ms < 1000)
        return std::to_string(ms) + "ms";

      if (ms < 10000)
      {
        const auto seconds = ms / 1000;
        const auto tenths = (ms % 1000) / 100;
        return std::to_string(seconds) + "." + std::to_string(tenths) + "s";
      }

      return std::to_string((ms + 500) / 1000) + "s";
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
      std::error_code ec;
      fs::create_directories(p.parent_path(), ec);

      const fs::path tmp = p.string() + ".tmp";
      {
        std::ofstream out(tmp);
        if (!out)
          throw std::runtime_error("cannot write: " + tmp.string());

        out << j.dump(2) << "\n";
      }

      fs::rename(tmp, p, ec);
      if (ec)
      {
        ec.clear();
        fs::remove(p, ec);
        ec.clear();
        fs::rename(tmp, p, ec);
        if (ec)
          throw std::runtime_error("cannot replace: " + p.string() + ": " + ec.message());
      }
    }

    static std::string sanitize_id_dot(const std::string &id)
    {
      std::string s = id;
      for (char &c : s)
      {
        if (c == '/')
          c = '.';
      }
      return s;
    }

    static std::string cmake_safe_target(const std::string &id)
    {
      std::string out = "vix__";
      for (char c : id)
      {
        if (std::isalnum(static_cast<unsigned char>(c)))
          out.push_back(c);
        else
          out += "__";
      }
      return out;
    }

    static std::string cmake_alias_target(const std::string &id)
    {
      const auto slash = id.find('/');
      if (slash == std::string::npos)
        return id;
      return id.substr(0, slash) + "::" + id.substr(slash + 1);
    }

    static bool starts_with_local(const std::string &s, const std::string &prefix);
    static bool is_supported_git_url(const std::string &url);

    static std::vector<std::string> cmake_dependency_aliases(const DepResolved &dep)
    {
      std::vector<std::string> aliases;
      aliases.reserve(dep.dependencies.size());

      for (const std::string &id : dep.dependencies)
      {
        const std::string alias = cmake_alias_target(id);

        if (!alias.empty())
          aliases.push_back(alias);
      }

      std::sort(aliases.begin(), aliases.end());
      aliases.erase(std::unique(aliases.begin(), aliases.end()), aliases.end());

      return aliases;
    }

    static fs::path store_checkout_path(const std::string &id, const std::string &commit)
    {
      return store_git_dir() / sanitize_id_dot(id) / commit;
    }

    static fs::path git_cache_checkout_path(const std::string &url, const std::string &commit);
    static int install_project_dependencies();

    static fs::path entry_path(const std::string &ns, const std::string &name)
    {
      return registry_index_dir() / (ns + "." + name + ".json");
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

    static int resolve_version_v1(const json &entry, PkgSpec &spec)
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
        versions.push_back(it.key());

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

    static int ensure_registry_present()
    {
      if (fs::exists(registry_dir()) && fs::exists(registry_index_dir()))
        return 0;

      vix::cli::util::err_line(std::cerr, "registry not synced");
      vix::cli::util::warn_line(std::cerr, "Run: vix registry sync");
      return 1;
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
        const std::string cmd = "git clone -q " + repoUrl + " " + dst.string();
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

    static void remove_all_if_exists(const fs::path &p)
    {
      std::error_code ec;

      const auto status = fs::symlink_status(p, ec);
      if (ec)
        return;

      if (status.type() == fs::file_type::not_found)
        return;

      fs::remove_all(p, ec);
    }

    static void ensure_symlink_or_copy_dir(const fs::path &src, const fs::path &dst)
    {
      std::error_code ec;
      remove_all_if_exists(dst);

#ifdef _WIN32
      fs::create_directories(dst, ec);
      fs::copy(
          src,
          dst,
          fs::copy_options::recursive |
              fs::copy_options::copy_symlinks |
              fs::copy_options::overwrite_existing,
          ec);
      if (ec)
        throw std::runtime_error("failed to copy dependency: " + dst.string());
#else
      fs::create_directories(dst.parent_path(), ec);
      fs::create_directory_symlink(src, dst, ec);
      if (ec)
      {
        ec.clear();
        fs::create_directories(dst, ec);
        fs::copy(
            src,
            dst,
            fs::copy_options::recursive |
                fs::copy_options::copy_symlinks |
                fs::copy_options::overwrite_existing,
            ec);
        if (ec)
          throw std::runtime_error("failed to link/copy dependency: " + dst.string());
      }
#endif
    }

    static std::string next_arg_value(const std::vector<std::string> &args, std::size_t &i, const std::string &flag)
    {
      if (i + 1 >= args.size())
        throw std::runtime_error("missing value for " + flag);
      ++i;
      return args[i];
    }

    static ParsedArgs parse_args(const std::vector<std::string> &args)
    {
      ParsedArgs parsed;

      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &arg = args[i];

        if (arg == "-g" || arg == "--global")
        {
          parsed.globalMode = true;

          if (i + 1 < args.size())
          {
            parsed.globalSpec = args[i + 1];
            ++i;
          }
        }
        else if (arg == "--yes" || arg == "-y")
        {
          parsed.yes = true;
        }
        else if (arg == "--name")
          parsed.gitName = next_arg_value(args, i, arg);
        else if (arg == "--tag")
          parsed.gitTag = next_arg_value(args, i, arg);
        else if (arg == "--branch")
          parsed.gitBranch = next_arg_value(args, i, arg);
        else if (arg == "--rev" || arg == "--commit")
          parsed.gitRev = next_arg_value(args, i, arg);
        else if (arg == "--target")
          parsed.gitTarget = next_arg_value(args, i, arg);
        else if (arg == "--subdirectory" || arg == "--subdir")
          parsed.gitSubdirectory = next_arg_value(args, i, arg);
        else if (arg == "--include")
          parsed.gitInclude = next_arg_value(args, i, arg);
        else if (arg == "--header-only" || arg == "--headers")
          parsed.gitHeaderOnly = true;
        else if (arg == "--pre" || arg == "--prerelease")
          parsed.allowPrerelease = true;
        else if (starts_with_local(arg, "--name="))
          parsed.gitName = arg.substr(std::string("--name=").size());
        else if (starts_with_local(arg, "--tag="))
          parsed.gitTag = arg.substr(std::string("--tag=").size());
        else if (starts_with_local(arg, "--branch="))
          parsed.gitBranch = arg.substr(std::string("--branch=").size());
        else if (starts_with_local(arg, "--rev="))
          parsed.gitRev = arg.substr(std::string("--rev=").size());
        else if (starts_with_local(arg, "--target="))
          parsed.gitTarget = arg.substr(std::string("--target=").size());
        else if (starts_with_local(arg, "--subdirectory="))
          parsed.gitSubdirectory = arg.substr(std::string("--subdirectory=").size());
        else if (starts_with_local(arg, "--include="))
          parsed.gitInclude = arg.substr(std::string("--include=").size());
        else if (parsed.globalMode && parsed.globalSpec.empty())
        {
          parsed.globalSpec = arg;
        }
        else if (parsed.gitSpec.empty() && is_supported_git_url(arg))
        {
          parsed.gitSpec = arg;
        }
      }

      split_compact_git_revision(parsed);
      return parsed;
    }

    static std::string shell_quote_integrity(const std::string &s)
    {
      std::string out;
      out.reserve(s.size() + 2);
      out.push_back('\'');
      for (char c : s)
      {
        if (c == '\'')
          out += "'\\''";
        else
          out.push_back(c);
      }
      out.push_back('\'');
      return out;
    }

    static std::string capture_integrity_command(const std::string &cmd, int &code)
    {
      return vix::cli::commands::helpers::run_and_capture_with_code(cmd, code);
    }

    static bool git_checkout_is_clean(const fs::path &checkout)
    {
      int code = 0;
      const std::string out = capture_integrity_command(
          "git -C " + shell_quote_integrity(checkout.string()) +
              " status --porcelain --untracked-files=no 2>/dev/null",
          code);
      return code == 0 && out.empty();
    }

    static bool git_checkout_head_matches(const fs::path &checkout, const std::string &commit)
    {
      if (commit.empty())
        return true;

      int code = 0;
      std::string out = capture_integrity_command(
          "git -C " + shell_quote_integrity(checkout.string()) +
              " rev-parse HEAD 2>/dev/null",
          code);
      out = trim_copy(out);
      return code == 0 && out == commit;
    }

    static bool lock_hash_metadata_is_current(const DepResolved &dep)
    {
      return dep.hashAlgorithm == vix::cli::util::PACKAGE_HASH_ALGORITHM &&
             dep.hashVersion == vix::cli::util::PACKAGE_HASH_VERSION;
    }

    enum class RegistryLockValidation
    {
      Match,
      Missing,
      Mismatch,
    };

    static RegistryLockValidation validate_locked_registry_metadata(
        const DepResolved &dep,
        std::string &reason)
    {
      reason.clear();

      if (dep.source == "git")
      {
        reason = "Git dependencies do not have registry metadata.";
        return RegistryLockValidation::Mismatch;
      }

      PkgSpec spec;
      if (!parse_pkg_spec(dep.id, spec))
      {
        reason = "invalid package id in vix.lock: " + dep.id;
        return RegistryLockValidation::Mismatch;
      }

      const fs::path p = entry_path(spec.ns, spec.name);
      if (!fs::exists(p))
      {
        reason = "registry metadata not available for " + dep.id;
        return RegistryLockValidation::Missing;
      }

      json entry;
      try
      {
        entry = read_json_or_throw(p);
      }
      catch (const std::exception &ex)
      {
        reason = std::string("cannot read registry metadata for ") + dep.id + ": " + ex.what();
        return RegistryLockValidation::Mismatch;
      }

      const std::string registryId = entry.value("id", dep.id);
      if (registryId != dep.id)
      {
        reason = "registry metadata id changed for " + dep.id;
        return RegistryLockValidation::Mismatch;
      }

      if (!entry.contains("repo") || !entry["repo"].is_object() ||
          !entry["repo"].contains("url") || !entry["repo"]["url"].is_string())
      {
        reason = "registry metadata is missing repo.url for " + dep.id;
        return RegistryLockValidation::Mismatch;
      }

      const std::string registryRepo = entry["repo"]["url"].get<std::string>();
      if (registryRepo != dep.repo)
      {
        reason = "registry metadata repo changed for " + dep.id;
        return RegistryLockValidation::Mismatch;
      }

      if (!entry.contains("versions") || !entry["versions"].is_object())
      {
        reason = "registry metadata is missing versions for " + dep.id;
        return RegistryLockValidation::Mismatch;
      }

      const json &versions = entry["versions"];
      if (!versions.contains(dep.version) || !versions[dep.version].is_object())
      {
        reason = "registry metadata no longer contains " + dep.id + "@" + dep.version;
        return RegistryLockValidation::Mismatch;
      }

      const json &version = versions[dep.version];
      if (!version.contains("tag") || !version["tag"].is_string() ||
          !version.contains("commit") || !version["commit"].is_string())
      {
        reason = "registry metadata is missing tag or commit for " + dep.id + "@" + dep.version;
        return RegistryLockValidation::Mismatch;
      }

      if (version["tag"].get<std::string>() != dep.tag)
      {
        reason = "registry metadata tag changed for " + dep.id + "@" + dep.version;
        return RegistryLockValidation::Mismatch;
      }

      if (version["commit"].get<std::string>() != dep.commit)
      {
        reason = "registry metadata commit changed for " + dep.id + "@" + dep.version;
        return RegistryLockValidation::Mismatch;
      }

      return RegistryLockValidation::Match;
    }

    static bool ensure_registry_metadata_still_matches_lock(
        const DepResolved &dep,
        std::string &reason)
    {
      RegistryLockValidation validation = validate_locked_registry_metadata(dep, reason);
      if (validation == RegistryLockValidation::Match)
        return true;
      if (validation == RegistryLockValidation::Mismatch)
        return false;

      const int syncRc = RegistryCommand::sync(true, false);
      if (syncRc == 0)
      {
        validation = validate_locked_registry_metadata(dep, reason);
        return validation == RegistryLockValidation::Match;
      }

      if (git_checkout_head_matches(dep.checkout, dep.commit) && git_checkout_is_clean(dep.checkout))
      {
        reason.clear();
        return true;
      }

      reason = "registry metadata is unavailable and the local checkout cannot validate the locked commit";
      return false;
    }

    static bool refresh_obsolete_integrity_metadata(
        DepResolved &dep,
        json &lockEntry,
        const std::string &actualHash,
        bool &printedHeader,
        bool &printedRefreshLine,
        std::string &refusalReason)
    {
      refusalReason.clear();

      if (lock_hash_metadata_is_current(dep))
        return false;

      if (dep.source == "git")
      {
        refusalReason = "automatic integrity metadata migration is only supported for registry packages";
        return false;
      }

      const bool clean = git_checkout_is_clean(dep.checkout);
      const bool headMatches = git_checkout_head_matches(dep.checkout, dep.commit);
      if (!clean || !headMatches)
        return false;

      if (!ensure_registry_metadata_still_matches_lock(dep, refusalReason))
        return false;

      if (!printedHeader)
      {
        vix::cli::util::section(std::cout, "Installing dependencies");
        printedHeader = true;
      }

      if (!printedRefreshLine)
      {
        std::cout << "  " << CYAN << "•" << RESET << " "
                  << GRAY << "refreshing obsolete integrity metadata" << RESET << "\n";
        printedRefreshLine = true;
      }

      dep.hash = actualHash;
      dep.hashAlgorithm = vix::cli::util::PACKAGE_HASH_ALGORITHM;
      dep.hashVersion = vix::cli::util::PACKAGE_HASH_VERSION;

      lockEntry["hash"] = dep.hash;
      lockEntry["hash_algorithm"] = dep.hashAlgorithm;
      lockEntry["hash_version"] = dep.hashVersion;

      std::cout << "  " << CYAN << "•" << RESET << " "
                << CYAN << BOLD << dep.id << RESET
                << GRAY << "@" << RESET
                << YELLOW << BOLD << dep.version << RESET
                << "  "
                << GRAY << "metadata updated" << RESET
                << "\n";

      return true;
    }

    static void print_integrity_recovery_hint(const DepResolved &dep, bool clean, bool headMatches)
    {
      if (!headMatches)
      {
        vix::cli::util::warn_line(std::cerr, "The cached checkout points at a different commit than vix.lock.");
        vix::cli::util::warn_line(std::cerr, "Preview project-scoped cleanup first: vix store gc --project --dry-run");
        return;
      }

      if (!clean)
      {
        vix::cli::util::warn_line(std::cerr, "The cached checkout has local modifications to tracked files.");
        vix::cli::util::warn_line(std::cerr, "Use: vix reset");
        vix::cli::util::warn_line(std::cerr, "If the shared store itself must be cleaned, preview first: vix store gc --project --dry-run");
        return;
      }

      if (!lock_hash_metadata_is_current(dep))
      {
        vix::cli::util::warn_line(std::cerr, "locked integrity metadata does not match the content produced by the current hash algorithm");
        vix::cli::util::warn_line(std::cerr, "Use: vix update");
        return;
      }

      vix::cli::util::warn_line(std::cerr, "The checkout is Git-clean, but its package hash differs from vix.lock.");
      vix::cli::util::warn_line(std::cerr, "This usually means registry metadata or lockfile integrity metadata is inconsistent.");
      vix::cli::util::warn_line(std::cerr, "Use: vix registry sync");
      vix::cli::util::warn_line(std::cerr, "If the mismatch persists with a fresh checkout, run: vix update");
    }

    static bool verify_dependency_hash(const DepResolved &dep)
    {
      if (dep.hash.empty())
        return true;

      const auto actualHashOpt = vix::cli::util::sha256_package_directory(dep.checkout);
      if (!actualHashOpt)
      {
        vix::cli::util::err_line(std::cerr, "integrity check failed: " + dep.id);
        vix::cli::util::warn_line(std::cerr, "could not compute package hash for checkout: " + dep.checkout.string());
        vix::cli::util::warn_line(std::cerr, "The checkout appears incomplete or unreadable.");
        vix::cli::util::warn_line(std::cerr, "Preview project-scoped cleanup first: vix store gc --project --dry-run");
        return false;
      }

      if (*actualHashOpt != dep.hash)
      {
        const bool clean = git_checkout_is_clean(dep.checkout);
        const bool headMatches = git_checkout_head_matches(dep.checkout, dep.commit);

        vix::cli::util::err_line(std::cerr, "integrity check failed: " + dep.id);
        vix::cli::util::err_line(std::cerr, "expected: " + dep.hash);
        vix::cli::util::err_line(std::cerr, "actual:   " + *actualHashOpt);
        print_integrity_recovery_hint(dep, clean, headMatches);
        return false;
      }

      return true;
    }

    static bool verify_dependency_hash_or_refresh(
        DepResolved &dep,
        json &lockEntry,
        bool &lockChanged,
        bool &printedHeader,
        bool &printedRefreshLine)
    {
      if (dep.hash.empty())
        return true;

      const auto actualHashOpt = vix::cli::util::sha256_package_directory(dep.checkout);
      if (!actualHashOpt)
      {
        vix::cli::util::err_line(std::cerr, "integrity check failed: " + dep.id);
        vix::cli::util::warn_line(std::cerr, "could not compute package hash for checkout: " + dep.checkout.string());
        vix::cli::util::warn_line(std::cerr, "The checkout appears incomplete or unreadable.");
        vix::cli::util::warn_line(std::cerr, "Preview project-scoped cleanup first: vix store gc --project --dry-run");
        return false;
      }

      if (*actualHashOpt == dep.hash)
        return true;

      std::string refusalReason;
      if (refresh_obsolete_integrity_metadata(
              dep,
              lockEntry,
              *actualHashOpt,
              printedHeader,
              printedRefreshLine,
              refusalReason))
      {
        lockChanged = true;
        return true;
      }

      const bool clean = git_checkout_is_clean(dep.checkout);
      const bool headMatches = git_checkout_head_matches(dep.checkout, dep.commit);

      vix::cli::util::err_line(std::cerr, "integrity check failed: " + dep.id);
      vix::cli::util::err_line(std::cerr, "expected: " + dep.hash);
      vix::cli::util::err_line(std::cerr, "actual:   " + *actualHashOpt);
      if (!refusalReason.empty())
      {
        vix::cli::util::warn_line(std::cerr, refusalReason);
        vix::cli::util::warn_line(std::cerr, "Refusing to rewrite vix.lock automatically.");
      }
      else
      {
        print_integrity_recovery_hint(dep, clean, headMatches);
      }
      return false;
    }

    static void load_dep_manifest(DepResolved &dep)
    {
      const fs::path manifest = dep.checkout / "vix.json";
      if (!fs::exists(manifest))
      {
        dep.type = "unknown";
        dep.include = "include";
        dep.dependencies.clear();
        dep.cmakeTargets.clear();
        return;
      }

      const json j = read_json_or_throw(manifest);

      dep.type = j.value("type", "");
      if (dep.type.empty())
        dep.type = "header-only";

      if (j.contains("include") && j["include"].is_string())
        dep.include = j["include"].get<std::string>();
      else
        dep.include = "include";

      dep.dependencies.clear();

      auto read_dep_block = [&](const json &d)
      {
        if (d.is_array())
        {
          for (const auto &item : d)
          {
            if (item.is_string())
            {
              dep.dependencies.push_back(item.get<std::string>());
            }
            else if (item.is_object())
            {
              const std::string id = item.value("id", "");
              if (!id.empty())
                dep.dependencies.push_back(id);
            }
          }
        }
        else if (d.is_object())
        {
          for (auto it = d.begin(); it != d.end(); ++it)
          {
            dep.dependencies.push_back(it.key());
          }
        }
      };

      dep.cmakeTargets.clear();

      auto append_target = [&](const std::string &target)
      {
        const std::string t = trim_copy(target);
        if (t.empty())
          return;

        if (std::find(dep.cmakeTargets.begin(), dep.cmakeTargets.end(), t) == dep.cmakeTargets.end())
          dep.cmakeTargets.push_back(t);
      };

      if (j.contains("cmake") && j["cmake"].is_object())
      {
        const auto &cmake = j["cmake"];

        if (cmake.contains("target") && cmake["target"].is_string())
          append_target(cmake["target"].get<std::string>());

        if (cmake.contains("targets") && cmake["targets"].is_array())
        {
          for (const auto &item : cmake["targets"])
          {
            if (item.is_string())
              append_target(item.get<std::string>());
          }
        }
      }

      if (j.contains("cmake_target") && j["cmake_target"].is_string())
        append_target(j["cmake_target"].get<std::string>());

      if (j.contains("dependencies"))
        read_dep_block(j["dependencies"]);

      if (dep.dependencies.empty() && j.contains("deps"))
        read_dep_block(j["deps"]);
    }

    static void generate_cmake(const std::vector<DepResolved> &deps);
    static std::vector<DepResolved> sort_deps_topologically(const std::vector<DepResolved> &deps);

    static std::vector<std::string> read_lock_dependency_specs(const fs::path &checkout)
    {
      std::vector<std::string> specs;
      const fs::path lock = checkout / "vix.lock";
      if (!fs::exists(lock))
        return specs;

      const json j = read_json_or_throw(lock);
      if (!j.contains("dependencies") || !j["dependencies"].is_array())
        return specs;

      for (const auto &item : j["dependencies"])
      {
        if (!item.is_object())
          continue;
        const std::string id = item.value("id", "");
        const std::string version = item.value("version", item.value("requested", ""));
        if (id.empty())
          continue;
        specs.push_back(version.empty() ? id : (id + "@" + version));
      }

      return specs;
    }

    static void generate_cmake_in_project(const fs::path &projectRoot, const std::vector<DepResolved> &deps)
    {
      const fs::path previous = fs::current_path();
      fs::current_path(projectRoot);
      try
      {
        generate_cmake(deps);
      }
      catch (...)
      {
        fs::current_path(previous);
        throw;
      }
      fs::current_path(previous);
    }

    static std::vector<DepResolved> prepare_global_project_deps(const DepResolved &rootDep, const std::vector<DepResolved> &ordered)
    {
      std::vector<DepResolved> deps;
      for (DepResolved dep : ordered)
      {
        if (dep.id == rootDep.id)
          continue;

        const fs::path link = rootDep.checkout / ".vix" / "deps" / sanitize_id_dot(dep.id);
        ensure_symlink_or_copy_dir(dep.checkout, link);
        dep.linkDir = link;
        deps.push_back(dep);
      }

      return sort_deps_topologically(deps);
    }

    static std::vector<DepResolved> sort_deps_topologically(const std::vector<DepResolved> &deps)
    {
      std::unordered_map<std::string, DepResolved> byId;
      std::unordered_map<std::string, std::vector<std::string>> graph;
      std::unordered_map<std::string, int> indegree;

      for (const auto &dep : deps)
      {
        byId[dep.id] = dep;
        indegree[dep.id] = 0;
      }

      for (const auto &dep : deps)
      {
        for (const auto &childId : dep.dependencies)
        {
          if (!byId.count(childId))
            continue;

          graph[childId].push_back(dep.id);
          indegree[dep.id]++;
        }
      }

      std::queue<std::string> q;
      for (const auto &[id, deg] : indegree)
      {
        if (deg == 0)
          q.push(id);
      }

      std::vector<DepResolved> ordered;
      ordered.reserve(deps.size());

      while (!q.empty())
      {
        const std::string id = q.front();
        q.pop();

        ordered.push_back(byId.at(id));

        auto it = graph.find(id);
        if (it == graph.end())
          continue;

        for (const auto &next : it->second)
        {
          indegree[next]--;
          if (indegree[next] == 0)
            q.push(next);
        }
      }

      if (ordered.size() != deps.size())
      {
        throw std::runtime_error(
            "dependency cycle detected while generating .vix/vix_deps.cmake");
      }

      return ordered;
    }

    static DepResolved resolve_dep_from_lock_entry(const json &d)
    {
      DepResolved dep;
      dep.id = d.value("id", "");
      dep.source = d.value("source", "registry");
      dep.version = d.value("version", "");
      dep.requested = d.value("requested", "");
      dep.repo = d.value("repo", d.value("url", ""));
      dep.tag = d.value("tag", "");
      dep.commit = d.value("commit", "");
      dep.hash = d.value("hash", "");
      dep.hashAlgorithm = d.value("hash_algorithm", "");
      dep.hashVersion = d.value("hash_version", 0);
      dep.subdirectory = d.value("subdirectory", "");
      dep.headerOnly = d.value("header_only", false);

      if (dep.id.empty() || dep.repo.empty() || dep.commit.empty())
        throw std::runtime_error("invalid dependency entry in vix.lock (missing id/repo/commit)");

      if (d.contains("targets") && d["targets"].is_array())
      {
        for (const auto &item : d["targets"])
        {
          if (item.is_string())
            dep.cmakeTargets.push_back(item.get<std::string>());
        }
      }
      else if (d.contains("target") && d["target"].is_string())
      {
        dep.cmakeTargets.push_back(d["target"].get<std::string>());
      }

      if (d.contains("includes") && d["includes"].is_array())
      {
        for (const auto &item : d["includes"])
        {
          if (item.is_string())
            dep.includes.push_back(item.get<std::string>());
        }
      }
      else if (d.contains("include") && d["include"].is_string())
      {
        dep.includes.push_back(d["include"].get<std::string>());
      }

      if (!dep.includes.empty())
        dep.include = dep.includes.front();

      if (d.contains("cmake_options") && d["cmake_options"].is_object())
      {
        for (auto it = d["cmake_options"].begin(); it != d["cmake_options"].end(); ++it)
        {
          if (it.value().is_boolean())
            dep.cmakeOptions.emplace_back(it.key(), it.value().get<bool>() ? "ON" : "OFF");
          else if (it.value().is_string())
            dep.cmakeOptions.emplace_back(it.key(), it.value().get<std::string>());
          else
            dep.cmakeOptions.emplace_back(it.key(), it.value().dump());
        }
      }

      if (dep.source == "git")
      {
        dep.checkout = git_cache_checkout_path(dep.repo, dep.commit);
        dep.type = dep.headerOnly ? "header-only" : "library";
      }
      else
      {
        dep.checkout = store_checkout_path(dep.id, dep.commit);
      }

      return dep;
    }

    static std::optional<DepResolved> resolve_package_from_registry(const std::string &rawSpec)
    {
      PkgSpec spec;
      if (!parse_pkg_spec(rawSpec, spec))
        return std::nullopt;

      const fs::path p = entry_path(spec.ns, spec.name);
      if (!fs::exists(p))
        return std::nullopt;

      const json entry = read_json_or_throw(p);

      if (resolve_version_v1(entry, spec) != 0)
        return std::nullopt;

      if (!entry.contains("repo") || !entry["repo"].is_object())
        throw std::runtime_error("invalid registry entry: missing repo object");

      if (!entry["repo"].contains("url") || !entry["repo"]["url"].is_string())
        throw std::runtime_error("invalid registry entry: missing repo.url");

      if (!entry.contains("versions") || !entry["versions"].is_object())
        throw std::runtime_error("invalid registry entry: missing versions object");

      const json versions = entry.at("versions");
      if (!versions.contains(spec.resolvedVersion))
      {
        throw std::runtime_error(
            "version not found: " + spec.id() + "@" + spec.resolvedVersion);
      }

      const json v = versions.at(spec.resolvedVersion);

      if (!v.contains("tag") || !v["tag"].is_string())
        throw std::runtime_error("invalid registry entry: missing version tag for " + spec.id());

      if (!v.contains("commit") || !v["commit"].is_string())
        throw std::runtime_error("invalid registry entry: missing version commit for " + spec.id());

      DepResolved dep;
      dep.id = spec.id();
      dep.version = spec.resolvedVersion;
      dep.repo = entry.at("repo").at("url").get<std::string>();
      dep.tag = v.at("tag").get<std::string>();
      dep.commit = v.at("commit").get<std::string>();
      dep.type = entry.value("type", "header-only");
      if (v.contains("extensions") && v["extensions"].is_object()) dep.extensions = v["extensions"];
      dep.checkout = store_checkout_path(dep.id, dep.commit);

      return dep;
    }

    static json load_global_manifest()
    {
      if (!fs::exists(global_manifest_path()))
      {
        return json{
            {"packages", json::array()}};
      }

      json root = read_json_or_throw(global_manifest_path());

      if (!root.is_object())
      {
        return json{
            {"packages", json::array()}};
      }

      if (!root.contains("packages") || !root["packages"].is_array())
        root["packages"] = json::array();

      return root;
    }
    static void save_global_manifest(const json &root)
    {
      fs::create_directories(global_root_dir());
      write_json_or_throw(global_manifest_path(), root);
    }

    static std::string now_utc_iso8601();
    static json make_files_json(const std::vector<fs::path> &files);
    static json make_strings_json(const std::vector<std::string> &items);

    static void save_global_install(
        const DepResolved &dep,
        const fs::path &installedPath,
        const std::vector<fs::path> &files,
        const std::vector<std::string> &executables,
        const std::vector<std::string> &shims)
    {
      json root = load_global_manifest();
      auto &arr = root["packages"];

      bool updated = false;

      for (auto &item : arr)
      {
        if (item.value("id", "") == dep.id)
        {
          item["id"] = dep.id;
          item["package"] = dep.id;
          item["version"] = dep.version;
          item["repo"] = dep.repo;
          item["tag"] = dep.tag;
          item["commit"] = dep.commit;
          item["hash"] = dep.hash;
          item["hash_algorithm"] = dep.hashAlgorithm.empty() ? vix::cli::util::PACKAGE_HASH_ALGORITHM : dep.hashAlgorithm;
          item["hash_version"] = dep.hashVersion > 0 ? dep.hashVersion : vix::cli::util::PACKAGE_HASH_VERSION;
          item["type"] = dep.type;
          item["include"] = dep.include;
          item["installed_path"] = installedPath.string();
          item["prefix"] = global_root_dir().string();
          item["installed_at"] = now_utc_iso8601();
          item["files"] = make_files_json(files);
          item["executables"] = make_strings_json(executables);
          item["shims"] = make_strings_json(shims);
          if (!dep.extensions.is_null()) item["extensions"] = dep.extensions;
          else item.erase("extensions");
          updated = true;
          break;
        }
      }

      if (!updated)
      {
        json newItem = {
            {"id", dep.id},
            {"package", dep.id},
            {"version", dep.version},
            {"repo", dep.repo},
            {"tag", dep.tag},
            {"commit", dep.commit},
            {"hash", dep.hash},
            {"hash_algorithm", dep.hashAlgorithm.empty() ? vix::cli::util::PACKAGE_HASH_ALGORITHM : dep.hashAlgorithm},
            {"hash_version", dep.hashVersion > 0 ? dep.hashVersion : vix::cli::util::PACKAGE_HASH_VERSION},
            {"type", dep.type},
            {"include", dep.include},
            {"installed_path", installedPath.string()},
            {"prefix", global_root_dir().string()},
            {"installed_at", now_utc_iso8601()},
            {"files", make_files_json(files)},
            {"executables", make_strings_json(executables)},
            {"shims", make_strings_json(shims)},
        };
        if (!dep.extensions.is_null()) newItem["extensions"] = dep.extensions;
        arr.push_back(std::move(newItem));
      }

      save_global_manifest(root);
    }

    static std::string cmake_quote(const std::string &s)
    {
      std::string out = "\"";
      for (char c : s)
      {
        if (c == '"')
          out += "\\\"";
        else
          out += c;
      }
      out += "\"";
      return out;
    }

    static std::string shell_quote(const std::string &s)
    {
      return vix::cli::commands::helpers::quote(s);
    }

    static bool starts_with_local(const std::string &s, const std::string &prefix)
    {
      return s.rfind(prefix, 0) == 0;
    }

    static bool is_supported_git_url(const std::string &url)
    {
      const std::string value = trim_copy(url);
      if (value.empty())
        return false;

      if (starts_with_local(value, "https://") ||
          starts_with_local(value, "http://") ||
          starts_with_local(value, "ssh://") ||
          starts_with_local(value, "git://") ||
          starts_with_local(value, "file://"))
        return true;

      if (value.find('@') != std::string::npos && value.find(':') != std::string::npos && value.find(' ') == std::string::npos)
        return true;

      std::error_code ec;
      return fs::exists(fs::path(value), ec) && !ec;
    }

    static bool is_safe_local_dep_name(const std::string &name)
    {
      if (name.empty())
        return false;
      const unsigned char first = static_cast<unsigned char>(name.front());
      if (!(std::isalpha(first) || name.front() == '_'))
        return false;
      for (char c : name)
      {
        const unsigned char ch = static_cast<unsigned char>(c);
        if (!(std::isalnum(ch) || c == '_' || c == '-'))
          return false;
      }
      return true;
    }

    static std::string git_url_to_default_name(std::string url)
    {
      url = trim_copy(std::move(url));
      while (!url.empty() && (url.back() == '/' || url.back() == '\\'))
        url.pop_back();

      std::size_t pos = url.find_last_of("/:");
      std::string name = pos == std::string::npos ? url : url.substr(pos + 1);
      if (name.size() > 4 && name.substr(name.size() - 4) == ".git")
        name = name.substr(0, name.size() - 4);

      std::string out;
      for (char c : name)
      {
        const unsigned char ch = static_cast<unsigned char>(c);
        if (std::isalnum(ch) || c == '_' || c == '-')
          out.push_back(c);
        else if (c == '.')
          out.push_back('-');
      }
      if (out.empty() || !(std::isalpha(static_cast<unsigned char>(out.front())) || out.front() == '_'))
        out = "dep_" + out;
      return out;
    }

    static std::string short_hash_for_url(const std::string &url)
    {
      return vix::cli::util::hex64(vix::cli::util::fnv1a64_str(url, 1469598103934665603ull));
    }

    static std::string capture_command_or_throw(const std::string &cmd, const std::string &what)
    {
      int code = 0;
      std::string out = vix::cli::commands::helpers::run_and_capture_with_code(cmd, code);
      if (code != 0)
        throw std::runtime_error(what + " failed");
      return trim_copy(out);
    }

    static std::string first_ls_remote_hash(const std::string &output)
    {
      std::istringstream in(output);
      std::string line;
      while (std::getline(in, line))
      {
        line = trim_copy(line);
        if (line.empty())
          continue;
        const auto tab = line.find_first_of(" \t");
        return tab == std::string::npos ? line : line.substr(0, tab);
      }
      return {};
    }

    static std::string semver_key_from_tag(std::string tag)
    {
      tag = trim_copy(std::move(tag));
      if (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V'))
        tag.erase(tag.begin());
      return tag;
    }

    static bool is_semver_tag(const std::string &tag, bool allowPrerelease)
    {
      const std::string v = semver_key_from_tag(tag);
      if (v.empty())
        return false;
      if (!allowPrerelease && v.find('-') != std::string::npos)
        return false;
      return vix::cli::util::semver::compare(v, v) == 0;
    }

    static std::string select_latest_stable_git_tag(const std::string &url, bool allowPrerelease)
    {
      int code = 0;
      const std::string out = vix::cli::commands::helpers::run_and_capture_with_code(
          "git ls-remote --tags " + shell_quote(url),
          code);
      if (code != 0)
        return {};

      std::vector<std::pair<std::string, std::string>> candidates;
      std::istringstream in(out);
      std::string line;
      while (std::getline(in, line))
      {
        line = trim_copy(line);
        const std::string needle = "refs/tags/";
        const std::size_t pos = line.find(needle);
        if (pos == std::string::npos)
          continue;
        std::string tag = line.substr(pos + needle.size());
        if (tag.size() > 3 && tag.rfind("^{}") == tag.size() - 3)
          tag.resize(tag.size() - 3);
        if (!is_semver_tag(tag, allowPrerelease))
          continue;
        candidates.push_back({semver_key_from_tag(tag), tag});
      }

      if (candidates.empty())
        return {};

      std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b)
                { return vix::cli::util::semver::compare(a.first, b.first) > 0; });
      return candidates.front().second;
    }

    static void split_compact_git_revision(ParsedArgs &parsed)
    {
      if (parsed.gitSpec.empty() || !parsed.gitTag.empty() || !parsed.gitBranch.empty() || !parsed.gitRev.empty())
        return;
      if (starts_with_local(parsed.gitSpec, "git@"))
        return;

      const std::size_t at = parsed.gitSpec.rfind('@');
      if (at == std::string::npos || at == 0 || at + 1 >= parsed.gitSpec.size())
        return;

      const std::size_t slash = parsed.gitSpec.find_last_of("/\\");
      if (slash != std::string::npos && at < slash)
        return;

      parsed.gitTag = parsed.gitSpec.substr(at + 1);
      parsed.gitSpec = parsed.gitSpec.substr(0, at);
    }

    static std::string resolve_git_commit_or_throw(
        const std::string &url,
        const std::string &tag,
        const std::string &branch,
        const std::string &rev)
    {
      const int revisionCount = (!tag.empty() ? 1 : 0) + (!branch.empty() ? 1 : 0) + (!rev.empty() ? 1 : 0);
      if (revisionCount > 1)
        throw std::runtime_error("use only one of --tag, --branch, or --rev");

      if (!tag.empty())
      {
        std::string out = capture_command_or_throw(
            "git ls-remote " + shell_quote(url) + " " + shell_quote("refs/tags/" + tag + "^{}"),
            "git ls-remote");
        std::string hash = first_ls_remote_hash(out);
        if (hash.empty())
        {
          out = capture_command_or_throw(
              "git ls-remote " + shell_quote(url) + " " + shell_quote("refs/tags/" + tag),
              "git ls-remote");
          hash = first_ls_remote_hash(out);
        }
        if (hash.empty())
          throw std::runtime_error("tag not found: " + tag);
        return hash;
      }

      if (!branch.empty())
      {
        const std::string out = capture_command_or_throw(
            "git ls-remote " + shell_quote(url) + " " + shell_quote("refs/heads/" + branch),
            "git ls-remote");
        const std::string hash = first_ls_remote_hash(out);
        if (hash.empty())
          throw std::runtime_error("branch not found: " + branch);
        return hash;
      }

      if (!rev.empty())
        return rev;

      const std::string out = capture_command_or_throw(
          "git ls-remote " + shell_quote(url) + " HEAD",
          "git ls-remote");
      const std::string hash = first_ls_remote_hash(out);
      if (hash.empty())
        throw std::runtime_error("could not resolve repository HEAD");
      return hash;
    }

    static fs::path git_cache_checkout_path(const std::string &url, const std::string &commit)
    {
      return git_cache_dir() / short_hash_for_url(url) / commit;
    }

    static fs::path clone_git_to_cache_or_throw(const std::string &url, const std::string &commit)
    {
      fs::create_directories(git_cache_dir());
      const fs::path dst = git_cache_checkout_path(url, commit);
      if (fs::exists(dst / ".git") || fs::exists(dst / "CMakeLists.txt") || fs::exists(dst / "include"))
        return dst;

      const fs::path parent = dst.parent_path();
      const fs::path tmp = parent / (dst.filename().string() + ".tmp");
      std::error_code ec;
      fs::create_directories(parent, ec);
      fs::remove_all(tmp, ec);

      int code = 0;
      std::string out = vix::cli::commands::helpers::run_and_capture_with_code(
          "git clone -q " + shell_quote(url) + " " + shell_quote(tmp.string()),
          code);
      if (code != 0)
        throw std::runtime_error("git clone failed for: " + url);

      out = vix::cli::commands::helpers::run_and_capture_with_code(
          "git -C " + shell_quote(tmp.string()) + " -c advice.detachedHead=false checkout -q " + shell_quote(commit),
          code);
      if (code != 0)
      {
        fs::remove_all(tmp, ec);
        throw std::runtime_error("git checkout failed for: " + commit);
      }

      fs::rename(tmp, dst, ec);
      if (ec)
      {
        fs::remove_all(dst, ec);
        ec.clear();
        fs::rename(tmp, dst, ec);
        if (ec)
          throw std::runtime_error("cannot move git cache checkout: " + ec.message());
      }

      return dst;
    }

    static bool safe_relative_path_string(const std::string &raw)
    {
      if (raw.empty())
        return false;
      const fs::path p(raw);
      if (p.is_absolute())
        return false;
      for (const auto &part : p)
      {
        if (part == "..")
          return false;
      }
      return true;
    }

    static std::vector<std::string> detect_cmake_targets_from_file(const fs::path &cmakeLists)
    {
      std::ifstream in(cmakeLists);
      if (!in)
        return {};

      std::vector<std::string> targets;
      std::string line;
      while (std::getline(in, line))
      {
        const std::string trimmed = trim_copy(line);
        const std::string lower = [&]()
        {
          std::string v = trimmed;
          std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c)
                         { return static_cast<char>(std::tolower(c)); });
          return v;
        }();

        const std::string key = "add_library(";
        const auto pos = lower.find(key);
        if (pos == std::string::npos)
          continue;
        std::string rest = trim_copy(trimmed.substr(pos + key.size()));
        if (rest.empty() || rest[0] == '$')
          continue;
        std::string name;
        for (char c : rest)
        {
          if (std::isspace(static_cast<unsigned char>(c)) || c == ')')
            break;
          name.push_back(c);
        }
        if (!name.empty() && name != "INTERFACE" && name != "STATIC" && name != "SHARED" && name != "MODULE")
          targets.push_back(name);

        const auto aliasPos = lower.find(" alias ");
        if (aliasPos != std::string::npos)
        {
          std::istringstream ss(rest);
          std::string aliasName;
          ss >> aliasName;
          if (!aliasName.empty())
            targets.push_back(aliasName);
        }
      }

      std::sort(targets.begin(), targets.end());
      targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
      return targets;
    }

    static std::string read_text_file_or_empty_for_detection(const fs::path &path)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        return {};
      std::ostringstream out;
      out << in.rdbuf();
      return out.str();
    }

    static void add_generated_alias_candidates(
        std::vector<std::string> &targets,
        const fs::path &cmakeLists,
        const std::string &repoName)
    {
      const std::string content = read_text_file_or_empty_for_detection(cmakeLists);
      if (content.find(repoName + "::${target}") == std::string::npos &&
          content.find(repoName + "::${TARGET}") == std::string::npos)
        return;

      if (std::find(targets.begin(), targets.end(), repoName) != targets.end())
        targets.push_back(repoName + "::" + repoName);

      std::sort(targets.begin(), targets.end());
      targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    }

    static std::string lower_string(std::string v)
    {
      std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c)
                     { return static_cast<char>(std::tolower(c)); });
      return v;
    }

    static std::string cmake_target_last_segment(const std::string &target)
    {
      const auto pos = target.rfind("::");
      return pos == std::string::npos ? target : target.substr(pos + 2);
    }

    static bool cmake_target_looks_private(const std::string &target)
    {
      const std::string t = lower_string(cmake_target_last_segment(target));
      return t == "test" || t == "tests" || t.find("test") != std::string::npos ||
             t == "example" || t == "examples" || t.find("example") != std::string::npos ||
             t == "benchmark" || t == "bench" || t.find("bench") != std::string::npos ||
             t == "doc" || t == "docs" || t == "install" || t == "uninstall" ||
             t == "package";
    }

    static std::optional<std::string> choose_cmake_target(
        const std::vector<std::string> &targets,
        const std::string &repoName)
    {
      const std::string repo = lower_string(repoName);
      int bestScore = -1;
      std::vector<std::string> best;

      for (const std::string &target : targets)
      {
        if (cmake_target_looks_private(target))
          continue;

        const std::string t = lower_string(target);
        const std::string last = lower_string(cmake_target_last_segment(target));
        int score = 10;
        if (t == repo + "::" + repo)
          score = 100;
        else if (t == repo)
          score = 90;
        else if (last == repo && t.find("::") != std::string::npos)
          score = 80;
        else if (t.rfind(repo + "::", 0) == 0)
          score = 70;
        else if (last.find(repo) != std::string::npos)
          score = 50;

        if (score > bestScore)
        {
          bestScore = score;
          best.clear();
          best.push_back(target);
        }
        else if (score == bestScore)
        {
          best.push_back(target);
        }
      }

      if (best.size() == 1)
        return best.front();

      std::vector<std::string> namespaced;
      for (const std::string &target : targets)
      {
        if (!cmake_target_looks_private(target) && target.find("::") != std::string::npos)
          namespaced.push_back(target);
      }
      if (namespaced.size() == 1)
        return namespaced.front();

      if (best.empty() && targets.size() == 1 && !cmake_target_looks_private(targets.front()))
        return targets.front();
      return std::nullopt;
    }

    static std::vector<std::string> detect_header_include_roots(const fs::path &sourceDir)
    {
      std::vector<std::string> out;
      for (const char *candidate : {"include", "single_include"})
      {
        std::error_code ec;
        if (fs::is_directory(sourceDir / candidate, ec) && !ec)
          out.push_back(candidate);
      }
      return out;
    }

    static std::vector<std::pair<std::string, std::string>> app_cmake_options_to_pairs(
        const std::vector<std::pair<std::string, std::string>> &options)
    {
      std::vector<std::pair<std::string, std::string>> out = options;
      std::sort(out.begin(), out.end());
      return out;
    }

    static void generate_cmake(const std::vector<DepResolved> &deps)
    {
      fs::create_directories(project_vix_dir());

      std::ofstream out(project_deps_cmake());
      if (!out)
        throw std::runtime_error("cannot write: " + project_deps_cmake().string());

      out << "cmake_minimum_required(VERSION 3.20)\n";
      out << "# ======================================================\n";
      out << "# Generated by: vix install\n";
      out << "# DO NOT EDIT MANUALLY\n";
      out << "# ======================================================\n\n";

      out << "set(_VIX_DEPS_DIR " << cmake_quote(project_deps_dir().string()) << ")\n\n";

      out << "# Vix installed package roots\n";
      out << "if(DEFINED ENV{VIX_ROOT} AND NOT \"$ENV{VIX_ROOT}\" STREQUAL \"\")\n";
      out << "  list(PREPEND CMAKE_PREFIX_PATH \"$ENV{VIX_ROOT}\")\n";
      out << "endif()\n\n";

      out << "if(DEFINED ENV{HOME} AND NOT \"$ENV{HOME}\" STREQUAL \"\")\n";
      out << "  list(APPEND CMAKE_PREFIX_PATH \"$ENV{HOME}/.vix\")\n";
      out << "  list(APPEND CMAKE_PREFIX_PATH \"$ENV{HOME}/.local\")\n";
      out << "endif()\n\n";

      out << "if(DEFINED ENV{USERPROFILE} AND NOT \"$ENV{USERPROFILE}\" STREQUAL \"\")\n";
      out << "  list(APPEND CMAKE_PREFIX_PATH \"$ENV{USERPROFILE}/.vix\")\n";
      out << "  list(APPEND CMAKE_PREFIX_PATH \"$ENV{USERPROFILE}/.local\")\n";
      out << "endif()\n\n";

      out << "# ------------------------------------------------------\n";
      out << "# Internal helpers generated by Vix\n";
      out << "# ------------------------------------------------------\n\n";

      out << "function(_vix_disable_dep_extras dep_ns dep_name)\n";
      out << "  string(TOUPPER \"${dep_ns}\" _VIX_NS_UPPER)\n";
      out << "  string(TOUPPER \"${dep_name}\" _VIX_NAME_UPPER)\n";
      out << "\n";
      out << "  # Generic knobs used by many projects\n";
      out << "  set(BUILD_TESTING OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(BUILD_TESTS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(ENABLE_TESTS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(TESTS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(UNIT_TESTS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(BUILD_EXAMPLES OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(ENABLE_EXAMPLES OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(EXAMPLES OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(BUILD_BENCHMARKS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(BENCHMARKS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(BUILD_DOCS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(ENABLE_DOCS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(DOCS OFF CACHE BOOL \"\" FORCE)\n";
      out << "\n";
      out << "  # Package-specific knobs, e.g. CNERIUM_HTTP_BUILD_TESTS\n";
      out << "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_BUILD_TESTING OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_BUILD_TESTS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_ENABLE_TESTS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_TESTS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_UNIT_TESTS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_BUILD_EXAMPLES OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_ENABLE_EXAMPLES OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_EXAMPLES OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_BUILD_BENCHMARKS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_BENCHMARKS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_BUILD_DOCS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_ENABLE_DOCS OFF CACHE BOOL \"\" FORCE)\n";
      out << "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_DOCS OFF CACHE BOOL \"\" FORCE)\n";
      out << "endfunction()\n\n";

      out << "function(_vix_bridge_alias canonical actual)\n";
      out << "  if(TARGET ${canonical})\n";
      out << "    return()\n";
      out << "  endif()\n";
      out << "  if(NOT TARGET ${actual})\n";
      out << "    return()\n";
      out << "  endif()\n";
      out << "\n";
      out << "  string(REPLACE \"::\" \"__\" _VIX_BRIDGE_SAFE ${canonical})\n";
      out << "  set(_VIX_BRIDGE_TARGET \"vix_bridge__${_VIX_BRIDGE_SAFE}\")\n";
      out << "\n";
      out << "  if(NOT TARGET ${_VIX_BRIDGE_TARGET})\n";
      out << "    add_library(${_VIX_BRIDGE_TARGET} INTERFACE)\n";
      out << "    target_link_libraries(${_VIX_BRIDGE_TARGET} INTERFACE ${actual})\n";
      out << "  endif()\n";
      out << "\n";
      out << "  if(NOT TARGET ${canonical})\n";
      out << "    add_library(${canonical} ALIAS ${_VIX_BRIDGE_TARGET})\n";
      out << "  endif()\n";
      out << "endfunction()\n\n";

      out << "function(_vix_try_bridge_for_dep dep_ns dep_name)\n";
      out << "  set(_VIX_CANONICAL \"${dep_ns}::${dep_name}\")\n";
      out << "  if(TARGET ${_VIX_CANONICAL})\n";
      out << "    return()\n";
      out << "  endif()\n";
      out << "\n";
      out << "  string(REPLACE \"-\" \"_\" _VIX_NS_UNDERSCORE \"${dep_ns}\")\n";
      out << "  string(REPLACE \".\" \"_\" _VIX_NS_UNDERSCORE \"${_VIX_NS_UNDERSCORE}\")\n";
      out << "\n";
      out << "  string(REPLACE \"-\" \"_\" _VIX_NAME_UNDERSCORE \"${dep_name}\")\n";
      out << "  string(REPLACE \".\" \"_\" _VIX_NAME_UNDERSCORE \"${_VIX_NAME_UNDERSCORE}\")\n";
      out << "\n";
      out << "  string(REPLACE \"-\" \"\" _VIX_NAME_FLAT \"${dep_name}\")\n";
      out << "  string(REPLACE \"_\" \"\" _VIX_NAME_FLAT \"${_VIX_NAME_FLAT}\")\n";
      out << "  string(REPLACE \".\" \"\" _VIX_NAME_FLAT \"${_VIX_NAME_FLAT}\")\n";
      out << "\n";
      out << "  set(_VIX_CANDIDATES\n";
      out << "    ${ARGN}\n";
      out << "    \"${dep_name}\"\n";
      out << "    \"${_VIX_NAME_UNDERSCORE}\"\n";
      out << "    \"${_VIX_NAME_FLAT}\"\n";
      out << "\n";
      out << "    \"${dep_name}::${dep_name}\"\n";
      out << "    \"${dep_name}::${_VIX_NAME_UNDERSCORE}\"\n";
      out << "    \"${_VIX_NAME_UNDERSCORE}::${_VIX_NAME_UNDERSCORE}\"\n";
      out << "    \"${_VIX_NAME_UNDERSCORE}::${dep_name}\"\n";
      out << "    \"${_VIX_NAME_FLAT}::${_VIX_NAME_FLAT}\"\n";
      out << "\n";
      out << "    \"${dep_ns}_${dep_name}\"\n";
      out << "    \"${dep_ns}_${_VIX_NAME_UNDERSCORE}\"\n";
      out << "    \"${_VIX_NS_UNDERSCORE}_${dep_name}\"\n";
      out << "    \"${_VIX_NS_UNDERSCORE}_${_VIX_NAME_UNDERSCORE}\"\n";
      out << "\n";
      out << "    \"${dep_ns}-${dep_name}\"\n";
      out << "    \"${dep_ns}.${dep_name}\"\n";
      out << "\n";
      out << "    \"${dep_ns}::${dep_name}\"\n";
      out << "    \"${dep_ns}::${_VIX_NAME_UNDERSCORE}\"\n";
      out << "    \"${_VIX_NS_UNDERSCORE}::${dep_name}\"\n";
      out << "    \"${_VIX_NS_UNDERSCORE}::${_VIX_NAME_UNDERSCORE}\"\n";
      out << "  )\n";
      out << "\n";
      out << "  foreach(_VIX_CAND IN LISTS _VIX_CANDIDATES)\n";
      out << "    if(TARGET ${_VIX_CAND})\n";
      out << "      _vix_bridge_alias(${_VIX_CANONICAL} ${_VIX_CAND})\n";
      out << "      return()\n";
      out << "    endif()\n";
      out << "  endforeach()\n";
      out << "endfunction()\n\n";

      out << "function(_vix_ensure_interface_dep canonical safe include_dir)\n";
      out << "  if(TARGET ${canonical})\n";
      out << "    return()\n";
      out << "  endif()\n";
      out << "\n";
      out << "  if(NOT TARGET ${safe})\n";
      out << "    add_library(${safe} INTERFACE)\n";
      out << "    if(EXISTS \"${include_dir}\")\n";
      out << "      target_include_directories(${safe} INTERFACE \"${include_dir}\")\n";
      out << "    endif()\n";
      out << "  endif()\n";
      out << "\n";
      out << "  if(NOT TARGET ${canonical})\n";
      out << "    add_library(${canonical} ALIAS ${safe})\n";
      out << "  endif()\n";
      out << "endfunction()\n\n";

      for (const auto &dep : deps)
      {
        const std::string safe = cmake_safe_target(dep.id);
        const std::string alias = cmake_alias_target(dep.id);
        const fs::path depSourceDir = dep.linkDir;
        const fs::path depCMake = depSourceDir / "CMakeLists.txt";
        const fs::path depIncludeDir = dep.linkDir / dep.include;
        const std::string buildDirName = "_vix_build_" + sanitize_id_dot(dep.id);

        const auto slash = dep.id.find('/');
        const std::string depNs = (slash == std::string::npos) ? dep.id : dep.id.substr(0, slash);
        const std::string depName = (slash == std::string::npos) ? dep.id : dep.id.substr(slash + 1);

        out << "# " << dep.id << " @" << dep.version << " (" << dep.commit << ")\n";

        if (dep.source == "git")
        {
          const fs::path gitSourceDir = dep.subdirectory.empty() ? dep.linkDir : (dep.linkDir / dep.subdirectory);
          const fs::path gitCMake = gitSourceDir / "CMakeLists.txt";
          const std::string gitAlias = "vix_git::" + dep.id;

          if (dep.headerOnly)
          {
            out << "if(NOT TARGET " << safe << ")\n";
            out << "  add_library(" << safe << " INTERFACE)\n";
            std::vector<std::string> includeDirs = dep.includes.empty() ? std::vector<std::string>{dep.include} : dep.includes;
            for (const std::string &includeDir : includeDirs)
            {
              const fs::path inc = gitSourceDir / includeDir;
              out << "  if(EXISTS " << cmake_quote(inc.string()) << ")\n";
              out << "    target_include_directories(" << safe << " INTERFACE " << cmake_quote(inc.string()) << ")\n";
              out << "  endif()\n";
            }
            out << "endif()\n";
            out << "if(NOT TARGET " << gitAlias << ")\n";
            out << "  add_library(" << gitAlias << " ALIAS " << safe << ")\n";
            out << "endif()\n";
          }
          else if (fs::exists(gitCMake))
          {
            for (const auto &option : dep.cmakeOptions)
              out << "set(" << option.first << " " << option.second << " CACHE STRING \"\" FORCE)\n";
            out << "if(EXISTS " << cmake_quote(gitCMake.string()) << ")\n";
            out << "  add_subdirectory("
                << cmake_quote(gitSourceDir.string()) << " "
                << cmake_quote((project_vix_dir() / buildDirName).string())
                << " EXCLUDE_FROM_ALL)\n";
            out << "endif()\n";
          }
          else
          {
            out << "message(FATAL_ERROR "
                << cmake_quote("Git dependency " + dep.id + " does not expose a supported CMake or header-only package")
                << ")\n";
          }

          out << "\n";
          continue;
        }

        const bool hasCMake = fs::exists(depCMake);
        const bool isHeaderOnly =
            dep.type == "header-only" ||
            dep.type == "header_only" ||
            dep.type == "headers";

        const bool isCompiledLike =
            dep.type == "library" ||
            dep.type == "header-and-source" ||
            dep.type == "header_and_source" ||
            dep.type == "headers-and-sources";

        if (isHeaderOnly)
        {
          out << "_vix_ensure_interface_dep("
              << alias << " "
              << safe << " "
              << cmake_quote(depIncludeDir.string())
              << ")\n";
        }
        else if (hasCMake)
        {
          out << "_vix_disable_dep_extras(" << depNs << " " << depName << ")\n";
          out << "if(EXISTS " << cmake_quote(depCMake.string()) << ")\n";
          out << "  add_subdirectory("
              << cmake_quote(depSourceDir.string()) << " "
              << cmake_quote((project_vix_dir() / buildDirName).string())
              << " EXCLUDE_FROM_ALL)\n";
          out << "endif()\n";
          out << "_vix_try_bridge_for_dep(" << depNs << " " << depName;

          for (const auto &target : dep.cmakeTargets)
            out << " " << target;

          out << ")\n";
          out << "_vix_ensure_interface_dep("
              << alias << " "
              << safe << " "
              << cmake_quote(depIncludeDir.string())
              << ")\n";
        }
        else if (isCompiledLike)
        {
          out << "message(FATAL_ERROR "
              << cmake_quote(
                     "Dependency " + dep.id +
                     " is a compiled package but no CMakeLists.txt was found in " +
                     depSourceDir.string())
              << ")\n";
        }
        else
        {
          if (fs::exists(depIncludeDir))
          {
            out << "if(NOT TARGET " << alias << ")\n";
            out << "  add_library(" << safe << " INTERFACE)\n";
            out << "  add_library(" << alias << " ALIAS " << safe << ")\n";
            out << "  target_include_directories(" << safe << " INTERFACE "
                << cmake_quote(depIncludeDir.string()) << ")\n";
            out << "endif()\n";
          }
          else
          {
            out << "message(WARNING "
                << cmake_quote("Unsupported Vix package type for " + dep.id + ": " + dep.type)
                << ")\n";
          }
        }

        const std::vector<std::string> dependencyAliases =
            cmake_dependency_aliases(dep);

        if (!dependencyAliases.empty())
        {
          out << "if(TARGET " << safe << ")\n";
          out << "  target_link_libraries(" << safe << " INTERFACE\n";

          for (const std::string &aliasDep : dependencyAliases)
            out << "    " << aliasDep << "\n";

          out << "  )\n";
          out << "endif()\n";
        }

        out << "\n";
      }

      out << "# ------------------------------------------------------\n";
      out << "# Vix aggregate dependency target\n";
      out << "# ------------------------------------------------------\n\n";

      out << "if(NOT TARGET vix__deps)\n";
      out << "  add_library(vix__deps INTERFACE)\n";
      out << "endif()\n\n";

      out << "if(NOT TARGET vix::deps)\n";
      out << "  add_library(vix::deps ALIAS vix__deps)\n";
      out << "endif()\n\n";

      for (const auto &dep : deps)
      {
        if (dep.source == "git")
        {
          if (dep.headerOnly || dep.cmakeTargets.empty())
          {
            const std::string alias = "vix_git::" + dep.id;
            out << "if(TARGET " << alias << ")\n";
            out << "  target_link_libraries(vix__deps INTERFACE " << alias << ")\n";
            out << "endif()\n";
          }

          for (const std::string &target : dep.cmakeTargets)
          {
            out << "if(TARGET " << target << ")\n";
            out << "  target_link_libraries(vix__deps INTERFACE " << target << ")\n";
            out << "else()\n";
            out << "  message(FATAL_ERROR " << cmake_quote("Git dependency " + dep.id + " target not found: " + target) << ")\n";
            out << "endif()\n";
          }
          continue;
        }

        const std::string alias = cmake_alias_target(dep.id);

        if (alias.empty())
          continue;

        out << "if(TARGET " << alias << ")\n";
        out << "  target_link_libraries(vix__deps INTERFACE " << alias << ")\n";
        out << "endif()\n";
      }

      out << "\n";
    }

    struct GlobalExecutableDecl
    {
      std::string name;
      std::string target;
    };

    static std::string now_utc_iso8601()
    {
      const auto now = std::chrono::system_clock::now();
      const std::time_t t = std::chrono::system_clock::to_time_t(now);
      std::tm tm{};
#ifdef _WIN32
      gmtime_s(&tm, &t);
#else
      gmtime_r(&t, &tm);
#endif
      char buf[32]{};
      std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
      return std::string(buf);
    }

    static bool is_safe_executable_name(const std::string &name)
    {
      if (name.empty() || name == "." || name == "..")
        return false;

      if (name.find('/') != std::string::npos ||
          name.find('\\') != std::string::npos ||
          name.find(':') != std::string::npos)
        return false;

      if (name.find("..") != std::string::npos)
        return false;

      for (char c : name)
      {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '_' || c == '-' || c == '.'))
          return false;
      }

      return true;
    }

    static std::vector<GlobalExecutableDecl> read_declared_executables(const fs::path &manifestPath)
    {
      std::vector<GlobalExecutableDecl> out;
      if (!fs::exists(manifestPath))
        return out;

      const json j = read_json_or_throw(manifestPath);

      auto add = [&](std::string name, std::string target)
      {
        name = trim_copy(std::move(name));
        target = trim_copy(std::move(target));
        if (name.empty())
          return;
        if (!is_safe_executable_name(name))
          throw std::runtime_error("invalid executable name in vix.json: " + name);
        for (const auto &item : out)
        {
          if (item.name == name)
            return;
        }
        out.push_back({name, target});
      };

      // Optional metadata for global CLI packages. CMake install() remains
      // authoritative; this only documents and validates expected commands.
      if (j.contains("bin") && j["bin"].is_object())
      {
        for (auto it = j["bin"].begin(); it != j["bin"].end(); ++it)
        {
          if (it.value().is_string())
            add(it.key(), it.value().get<std::string>());
        }
      }

      if (j.contains("executables") && j["executables"].is_array())
      {
        for (const auto &item : j["executables"])
        {
          if (!item.is_object())
            continue;
          add(item.value("name", ""), item.value("target", ""));
        }
      }

      return out;
    }

    static bool is_executable_filename(const fs::path &p)
    {
      const std::string name = p.filename().string();
      if (name.empty())
        return false;
#ifdef _WIN32
      const std::string ext = p.extension().string();
      return ext == ".exe" || ext == ".cmd" || ext == ".bat";
#else
      std::error_code ec;
      const auto perms = fs::status(p, ec).permissions();
      if (ec)
        return false;
      return (perms & fs::perms::owner_exec) != fs::perms::none ||
             (perms & fs::perms::group_exec) != fs::perms::none ||
             (perms & fs::perms::others_exec) != fs::perms::none;
#endif
    }

    static std::string executable_command_name(const fs::path &p)
    {
      std::string name = p.filename().string();
#ifdef _WIN32
      const std::string ext = p.extension().string();
      if (ext == ".exe" || ext == ".cmd" || ext == ".bat")
        name = p.stem().string();
#endif
      return name;
    }

    static bool is_header_only_type(const std::string &type)
    {
      return type == "header-only" || type == "header_only" || type == "headers";
    }

    static void copy_header_fallback_to_stage(const DepResolved &dep, const fs::path &stage)
    {
      if (dep.include.empty())
        return;

      const fs::path src = dep.checkout / dep.include;
      std::error_code ec;
      if (!fs::exists(src, ec) || !fs::is_directory(src, ec))
        return;

      const fs::path dst = stage / "include";
      fs::create_directories(dst, ec);
      if (ec)
        throw std::runtime_error("cannot create header fallback directory: " + dst.string());

      fs::copy(
          src,
          dst,
          fs::copy_options::recursive |
              fs::copy_options::copy_symlinks |
              fs::copy_options::overwrite_existing,
          ec);
      if (ec)
        throw std::runtime_error("cannot install header fallback for " + dep.id + ": " + ec.message());
    }

    static void copy_header_fallbacks_to_stage(const DepResolved &rootDep, const std::vector<DepResolved> &ordered, const fs::path &stage)
    {
      copy_header_fallback_to_stage(rootDep, stage);

      for (const auto &dep : ordered)
      {
        if (dep.id == rootDep.id)
          continue;
        if (is_header_only_type(dep.type))
          copy_header_fallback_to_stage(dep, stage);
      }
    }

    static std::vector<fs::path> collect_regular_files(const fs::path &root)
    {
      std::vector<fs::path> files;
      std::error_code ec;
      if (!fs::exists(root, ec))
        return files;

      for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
           !ec && it != end;
           it.increment(ec))
      {
        if (ec)
          break;

        const fs::path p = it->path();
        const auto st = fs::symlink_status(p, ec);
        if (ec)
          continue;

        if (fs::is_regular_file(st) || fs::is_symlink(st))
          files.push_back(fs::relative(p, root).lexically_normal());
      }

      std::sort(files.begin(), files.end());
      return files;
    }

    static std::vector<std::string> collect_installed_commands(const fs::path &prefix)
    {
      std::vector<std::string> commands;
      const fs::path bin = prefix / "bin";
      std::error_code ec;
      if (!fs::exists(bin, ec) || !fs::is_directory(bin, ec))
        return commands;

      for (fs::directory_iterator it(bin, ec), end; !ec && it != end; it.increment(ec))
      {
        if (ec)
          break;
        if (!it->is_regular_file(ec) && !it->is_symlink(ec))
          continue;
        if (!is_executable_filename(it->path()))
          continue;
        const std::string cmd = executable_command_name(it->path());
        if (!cmd.empty() && std::find(commands.begin(), commands.end(), cmd) == commands.end())
          commands.push_back(cmd);
      }

      std::sort(commands.begin(), commands.end());
      return commands;
    }

    static std::string package_name_from_id(const std::string &id)
    {
      const auto slash = id.find('/');
      return slash == std::string::npos ? id : id.substr(slash + 1);
    }

    static void copy_built_executable_to_stage(const fs::path &src, const fs::path &stage, const std::string &commandName)
    {
      if (!is_safe_executable_name(commandName))
        throw std::runtime_error("invalid executable name: " + commandName);

      const fs::path dst = stage / "bin" / commandName;
      std::error_code ec;
      fs::create_directories(dst.parent_path(), ec);
      if (ec)
        throw std::runtime_error("cannot create staged bin directory: " + dst.parent_path().string());

      fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
      if (ec)
        throw std::runtime_error("cannot stage built executable '" + commandName + "': " + ec.message());

#ifndef _WIN32
      fs::permissions(
          dst,
          fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
          fs::perm_options::add,
          ec);
#endif
    }

    static std::optional<fs::path> find_built_executable_by_name(const fs::path &buildDir, const std::string &name)
    {
      if (name.empty())
        return std::nullopt;

      const fs::path direct = buildDir / name;
      if (is_executable_filename(direct))
        return direct;

#ifdef _WIN32
      const fs::path directExe = buildDir / (name + ".exe");
      if (is_executable_filename(directExe))
        return directExe;
#endif

      return vix::commands::RunCommand::detail::resolve_runnable_executable(buildDir, name);
    }

    static void stage_built_executables_missing_from_install(
        const DepResolved &rootDep,
        const fs::path &buildDir,
        const fs::path &stage,
        const std::vector<GlobalExecutableDecl> &declared)
    {
      std::set<std::string> installed;
      for (const auto &cmd : collect_installed_commands(stage))
        installed.insert(cmd);

      if (!declared.empty())
      {
        for (const auto &decl : declared)
        {
          if (installed.contains(decl.name))
            continue;

          std::optional<fs::path> exe = find_built_executable_by_name(buildDir, decl.name);

          if (!exe && !decl.target.empty())
            exe = find_built_executable_by_name(buildDir, decl.target);

          if (!exe)
            continue;

          copy_built_executable_to_stage(*exe, stage, decl.name);
          installed.insert(decl.name);
        }
        return;
      }

      if (!installed.empty())
        return;

      std::optional<fs::path> exe = find_built_executable_by_name(buildDir, package_name_from_id(rootDep.id));

      if (!exe)
        exe = vix::commands::RunCommand::detail::resolve_runnable_executable(buildDir);

      if (!exe)
        return;

      copy_built_executable_to_stage(*exe, stage, executable_command_name(*exe));
    }

    static std::string owner_of_relpath(const json &root, const fs::path &rel)
    {
      const std::string key = rel.generic_string();
      if (!root.contains("packages") || !root["packages"].is_array())
        return {};

      for (const auto &pkg : root["packages"])
      {
        if (!pkg.contains("files") || !pkg["files"].is_array())
          continue;
        for (const auto &file : pkg["files"])
        {
          if (file.is_string() && file.get<std::string>() == key)
            return pkg.value("id", "");
        }
      }

      return {};
    }

    static const json *find_global_pkg(const json &root, const std::string &id)
    {
      if (!root.contains("packages") || !root["packages"].is_array())
        return nullptr;
      for (const auto &pkg : root["packages"])
      {
        if (pkg.value("id", "") == id)
          return &pkg;
      }
      return nullptr;
    }

    static void remove_empty_parents_under(const fs::path &prefix, fs::path dir)
    {
      std::error_code ec;
      const fs::path root = fs::absolute(prefix, ec).lexically_normal();
      while (!dir.empty())
      {
        ec.clear();
        fs::path abs = fs::absolute(dir, ec).lexically_normal();
        if (ec || abs == root || abs.string().find(root.string()) != 0)
          break;
        if (!fs::is_directory(abs, ec) || ec)
          break;
        if (!fs::is_empty(abs, ec) || ec)
          break;
        fs::remove(abs, ec);
        if (ec)
          break;
        dir = abs.parent_path();
      }
    }

    static void copy_staged_prefix(const fs::path &stage, const fs::path &prefix, const std::vector<fs::path> &files)
    {
      for (const auto &rel : files)
      {
        const fs::path src = stage / rel;
        const fs::path dst = prefix / rel;
        std::error_code ec;
        fs::create_directories(dst.parent_path(), ec);
        if (ec)
          throw std::runtime_error("cannot create directory: " + dst.parent_path().string());
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec)
          throw std::runtime_error("cannot install file: " + rel.generic_string() + ": " + ec.message());
      }
    }

    static void remove_obsolete_registered_files_for_package(
        const json &pkg,
        const fs::path &prefix,
        const std::vector<fs::path> &newFiles)
    {
      if (!pkg.contains("files") || !pkg["files"].is_array())
        return;

      std::set<std::string> keep;
      for (const auto &file : newFiles)
        keep.insert(file.generic_string());

      std::vector<fs::path> dirs;
      for (const auto &file : pkg["files"])
      {
        if (!file.is_string())
          continue;

        const std::string relString = file.get<std::string>();
        if (keep.contains(relString))
          continue;

        const fs::path rel = fs::path(relString).lexically_normal();
        if (rel.empty() || rel.is_absolute() || rel.generic_string().find("..") != std::string::npos)
          continue;

        const fs::path abs = prefix / rel;
        std::error_code ec;
        fs::remove(abs, ec);
        dirs.push_back(abs.parent_path());
      }

      std::sort(dirs.begin(), dirs.end(), [](const fs::path &a, const fs::path &b)
                { return a.string().size() > b.string().size(); });
      for (const auto &dir : dirs)
        remove_empty_parents_under(prefix, dir);
    }

    static void validate_staged_paths(const std::vector<fs::path> &files)
    {
      for (const auto &rel : files)
      {
        const std::string s = rel.generic_string();
        if (rel.empty() || rel.is_absolute() || s.find("..") != std::string::npos)
          throw std::runtime_error("unsafe installed path from CMake: " + s);
      }
    }

    static void ensure_no_global_conflicts(const json &registry, const std::string &pkgId, const fs::path &prefix, const std::vector<fs::path> &files)
    {
      for (const auto &rel : files)
      {
        const fs::path dst = prefix / rel;
        std::error_code ec;
        if (!fs::exists(dst, ec) && !fs::is_symlink(dst, ec))
          continue;

        const std::string owner = owner_of_relpath(registry, rel);
        if (owner.empty())
        {
          throw std::runtime_error("refusing to overwrite unmanaged file: " + dst.string());
        }

        if (owner != pkgId)
        {
          throw std::runtime_error(
              "cannot install file '" + rel.generic_string() + "'; already owned by " + owner);
        }
      }
    }

    static void validate_declared_executables_installed(const std::vector<GlobalExecutableDecl> &declared, const std::vector<std::string> &installed)
    {
      for (const auto &decl : declared)
      {
        if (std::find(installed.begin(), installed.end(), decl.name) == installed.end())
          throw std::runtime_error("declared executable was not installed by CMake: " + decl.name);
      }
    }

    static bool path_has_dir(const fs::path &dir)
    {
      const char *env = vix::utils::vix_getenv("PATH");
      if (!env)
        return false;

      const std::string want = fs::absolute(dir).lexically_normal().string();
#ifdef _WIN32
      const char sep = ';';
#else
      const char sep = ':';
#endif
      std::string cur;
      std::istringstream iss(env);
      while (std::getline(iss, cur, sep))
      {
        if (cur.empty())
          continue;
        std::error_code ec;
        fs::path p = fs::absolute(fs::path(cur), ec).lexically_normal();
        if (!ec && p.string() == want)
          return true;
      }
      return false;
    }

    static std::vector<fs::path> current_path_dirs()
    {
      std::vector<fs::path> dirs;
      const char *env = vix::utils::vix_getenv("PATH");
      if (!env)
        return dirs;

#ifdef _WIN32
      const char sep = ';';
#else
      const char sep = ':';
#endif
      std::string cur;
      std::istringstream iss(env);
      while (std::getline(iss, cur, sep))
      {
        if (cur.empty())
          continue;
        std::error_code ec;
        fs::path p = fs::absolute(fs::path(cur), ec).lexically_normal();
        if (!ec)
          dirs.push_back(p);
      }

      return dirs;
    }

    static bool current_path_contains_exact_dir(const fs::path &dir)
    {
      const fs::path want = fs::absolute(dir).lexically_normal();
      for (const auto &entry : current_path_dirs())
      {
        if (entry == want)
          return true;
      }
      return false;
    }

    static std::optional<fs::path> immediate_user_shim_dir()
    {
      const std::string h = home_dir();
      if (h.empty())
        return std::nullopt;

      const fs::path home = fs::absolute(fs::path(h)).lexically_normal();
      const fs::path localBin = (home / ".local" / "bin").lexically_normal();
      if (current_path_contains_exact_dir(localBin))
        return localBin;

      const fs::path homeBin = (home / "bin").lexically_normal();
      if (current_path_contains_exact_dir(homeBin))
        return homeBin;

      for (const auto &dir : current_path_dirs())
      {
        const std::string d = dir.generic_string();
        const std::string hs = home.generic_string();

        if (d == hs || d.rfind(hs + '/', 0) == 0)
          return dir;
      }
      return std::nullopt;
    }

    static bool package_owns_shim(const json &registry, const std::string &pkgId, const fs::path &shim)
    {
      const std::string abs = fs::absolute(shim).lexically_normal().string();
      if (!registry.contains("packages") || !registry["packages"].is_array())
        return false;

      for (const auto &pkg : registry["packages"])
      {
        if (pkg.value("id", "") != pkgId)
          continue;
        if (!pkg.contains("shims") || !pkg["shims"].is_array())
          continue;
        for (const auto &item : pkg["shims"])
        {
          if (item.is_string() && fs::absolute(fs::path(item.get<std::string>())).lexically_normal().string() == abs)
            return true;
        }
      }

      return false;
    }

    static std::vector<std::string> install_immediate_command_shims(
        const json &registry,
        const std::string &pkgId,
        const std::vector<std::string> &commands)
    {
      std::vector<std::string> shims;

#ifdef _WIN32
      (void)registry;
      (void)pkgId;
      (void)commands;
      return shims;
#else
      if (path_has_dir(global_bin_dir()))
        return shims;

      const auto shimDirOpt = immediate_user_shim_dir();
      if (!shimDirOpt)
        return shims;

      const fs::path shimDir = *shimDirOpt;
      std::error_code ec;
      fs::create_directories(shimDir, ec);
      if (ec)
        return shims;

      for (const auto &cmd : commands)
      {
        if (!is_safe_executable_name(cmd))
          continue;

        const fs::path target = fs::absolute(global_bin_dir() / cmd).lexically_normal();
        const fs::path shim = fs::absolute(shimDir / cmd).lexically_normal();

        ec.clear();
        const auto st = fs::symlink_status(shim, ec);
        const bool exists = !ec && st.type() != fs::file_type::not_found;

        if (exists)
        {
          if (fs::is_symlink(st))
          {
            ec.clear();
            const fs::path rawTarget = fs::read_symlink(shim, ec);
            const fs::path linkTarget = rawTarget.is_absolute()
                                            ? rawTarget.lexically_normal()
                                            : fs::absolute(shim.parent_path() / rawTarget).lexically_normal();
            if (!ec && linkTarget == target)
            {
              shims.push_back(shim.string());
              continue;
            }
          }

          if (!package_owns_shim(registry, pkgId, shim))
            throw std::runtime_error("cannot install command '" + cmd + "'; shim already exists: " + shim.string());

          ec.clear();
          fs::remove(shim, ec);
          if (ec)
            throw std::runtime_error("cannot replace command shim: " + shim.string());
        }

        ec.clear();
        fs::create_symlink(target, shim, ec);
        if (ec)
          throw std::runtime_error("cannot create command shim: " + shim.string() + ": " + ec.message());

        shims.push_back(shim.string());
      }

      return shims;
#endif
    }

    static bool file_contains_text(const fs::path &path, const std::string &needleText)
    {
      std::ifstream in(path);
      if (!in)
        return false;

      std::ostringstream ss;
      ss << in.rdbuf();
      return ss.str().find(needleText) != std::string::npos;
    }

    static void append_line_once(const fs::path &path, const std::string &marker, const std::string &line)
    {
      std::error_code ec;
      fs::create_directories(path.parent_path(), ec);

      if (file_contains_text(path, marker))
        return;

      std::ofstream out(path, std::ios::app);
      if (!out)
        return;

      out << "\n# Vix global commands\n"
          << line << "\n";
    }

#ifndef _WIN32
    static fs::path default_shell_config_file()
    {
      const std::string h = home_dir();
      const fs::path home = h.empty() ? fs::current_path() : fs::path(h);
      const char *shellEnv = vix::utils::vix_getenv("SHELL");
      const std::string shell = shellEnv ? fs::path(shellEnv).filename().string() : std::string();

      if (shell == "zsh")
        return home / ".zshrc";

      if (shell == "fish")
        return home / ".config" / "fish" / "config.fish";

      return home / ".bashrc";
    }
#endif

#ifdef _WIN32
    static bool ensure_user_path_contains_global_bin()
    {
      if (path_has_dir(global_bin_dir()))
        return true;

      const std::string bin = fs::absolute(global_bin_dir()).lexically_normal().string();

      HKEY key = nullptr;
      const LONG openRc = RegOpenKeyExA(
          HKEY_CURRENT_USER,
          "Environment",
          0,
          KEY_READ | KEY_WRITE,
          &key);

      if (openRc != ERROR_SUCCESS)
        return false;

      char buffer[32767]{};
      DWORD size = sizeof(buffer);
      DWORD type = 0;
      std::string current;

      const LONG queryRc = RegQueryValueExA(key, "Path", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
      if (queryRc == ERROR_SUCCESS && (type == REG_EXPAND_SZ || type == REG_SZ))
        current.assign(buffer, strnlen(buffer, sizeof(buffer)));

      if (!current.empty() && current.back() != ';')
        current += ';';
      current += bin;

      const LONG setRc = RegSetValueExA(
          key,
          "Path",
          0,
          REG_EXPAND_SZ,
          reinterpret_cast<const BYTE *>(current.c_str()),
          static_cast<DWORD>(current.size() + 1));
      RegCloseKey(key);

      if (setRc != ERROR_SUCCESS)
        return false;

      SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>("Environment"), SMTO_ABORTIFHUNG, 5000, nullptr);
      return true;
    }
#else
    static bool ensure_user_path_contains_global_bin()
    {
      if (path_has_dir(global_bin_dir()))
        return true;

      const std::string h = home_dir();
      const fs::path bin = fs::absolute(global_bin_dir()).lexically_normal();
      const fs::path expectedDefault = h.empty() ? fs::path() : (fs::path(h) / ".vix" / "global" / "bin");
      const bool defaultPrefix = !h.empty() && bin == fs::absolute(expectedDefault).lexically_normal();

      const char *shellEnv = vix::utils::vix_getenv("SHELL");
      const std::string shell = shellEnv ? fs::path(shellEnv).filename().string() : std::string();
      const fs::path config = default_shell_config_file();

      if (shell == "fish")
      {
        const std::string marker = defaultPrefix ? "$HOME/.vix/global/bin" : bin.string();
        const std::string line = defaultPrefix
                                     ? "fish_add_path -g \"$HOME/.vix/global/bin\""
                                     : "fish_add_path -g \"" + bin.string() + "\"";
        append_line_once(config, marker, line);
        return true;
      }

      const std::string marker = defaultPrefix ? "$HOME/.vix/global/bin" : bin.string();
      const std::string line = defaultPrefix
                                   ? "export PATH=\"$HOME/.vix/global/bin:$PATH\""
                                   : "export PATH=\"" + bin.string() + ":$PATH\"";
      append_line_once(config, marker, line);
      return true;
    }
#endif

    static int run_checked_command(const std::string &cmd, const std::string &what)
    {
      int code = 0;
      const std::string out = vix::cli::commands::helpers::run_and_capture_with_code(cmd, code);
      if (code != 0)
      {
        if (!out.empty())
          std::cerr << out;
        vix::cli::util::err_line(std::cerr, what + " failed");
        return 1;
      }
      return 0;
    }

    static json make_files_json(const std::vector<fs::path> &files)
    {
      json arr = json::array();
      for (const auto &file : files)
        arr.push_back(file.generic_string());
      return arr;
    }

    static json make_strings_json(const std::vector<std::string> &items)
    {
      json arr = json::array();
      for (const auto &item : items)
        arr.push_back(item);
      return arr;
    }

    static void print_next_steps(
        std::size_t installedCount,
        std::size_t checkedCount,
        bool generatedCmake)
    {
      vix::cli::util::ok_line(std::cout, "Dependencies ready");

      if (installedCount > 0)
      {
        const std::string packageWord =
            installedCount == 1 ? "package" : "packages";

        vix::cli::util::info(
            std::cout,
            std::to_string(installedCount) + " " + packageWord + " installed");
      }

      vix::cli::util::info(
          std::cout,
          "checked " + std::to_string(checkedCount) + " package(s)");

      if (generatedCmake)
      {
        vix::cli::util::info(std::cout, "Generated .vix/vix_deps.cmake");
      }
    }

    static int install_global_package(const std::string &specRaw)
    {
      const auto started = std::chrono::steady_clock::now();

      if (ensure_registry_present() != 0)
        return 1;

      PkgSpec requestedRootSpec;
      if (!parse_pkg_spec(specRaw, requestedRootSpec))
      {
        vix::cli::util::err_line(std::cerr, "invalid package spec: " + specRaw);
        return 1;
      }
      const std::string requestedRootId = requestedRootSpec.id();

      std::unordered_map<std::string, DepResolved> resolvedById;
      std::queue<std::string> pendingSpecs;
      pendingSpecs.push(specRaw);

      try
      {
        while (!pendingSpecs.empty())
        {
          const std::string currentSpec = pendingSpecs.front();
          pendingSpecs.pop();

          std::optional<DepResolved> resolvedOpt = resolve_package_from_registry(currentSpec);
          if (!resolvedOpt)
          {
            vix::cli::util::err_line(std::cerr, "invalid package spec or package not found: " + currentSpec);
            vix::cli::util::warn_line(std::cerr, "Expected: @namespace/name[@version]");
            vix::cli::util::warn_line(std::cerr, "Example: vix install -g @gk/jwt@1.0.0");
            return 1;
          }

          DepResolved dep = *resolvedOpt;

          if (resolvedById.find(dep.id) != resolvedById.end())
            continue;

          std::string outDir;
          const int rc = clone_checkout(dep.repo, sanitize_id_dot(dep.id), dep.commit, outDir);
          if (rc != 0)
          {
            vix::cli::util::err_line(std::cerr, "fetch failed: " + dep.id);
            vix::cli::util::warn_line(std::cerr, "Check git access, network, or registry metadata.");
            return rc;
          }

          dep.checkout = fs::path(outDir);
          dep.hash = vix::cli::util::sha256_package_directory(dep.checkout).value_or("");
          dep.hashAlgorithm = vix::cli::util::PACKAGE_HASH_ALGORITHM;
          dep.hashVersion = vix::cli::util::PACKAGE_HASH_VERSION;

          if (!verify_dependency_hash(dep))
          {
            return 1;
          }

          load_dep_manifest(dep);

          if (dep.id == requestedRootId)
          {
            try
            {
              for (const auto &lockedSpec : read_lock_dependency_specs(dep.checkout))
                pendingSpecs.push(lockedSpec);
            }
            catch (const std::exception &ex)
            {
              vix::cli::util::err_line(std::cerr, std::string("invalid package lock: ") + ex.what());
              return 1;
            }
          }

          resolvedById[dep.id] = dep;

          for (const auto &childId : dep.dependencies)
          {
            if (resolvedById.find(childId) == resolvedById.end())
              pendingSpecs.push(childId);
          }
        }
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, std::string("install failed: ") + ex.what());
        return 1;
      }

      std::vector<DepResolved> resolved;
      resolved.reserve(resolvedById.size());

      for (auto &[_, dep] : resolvedById)
        resolved.push_back(dep);

      std::vector<DepResolved> ordered;
      try
      {
        ordered = sort_deps_topologically(resolved);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, std::string("install failed: ") + ex.what());
        return 1;
      }

      fs::create_directories(global_pkgs_dir());
      fs::create_directories(global_bin_dir());
      fs::create_directories(global_build_dir());
      fs::create_directories(global_tmp_dir());

      for (auto &dep : ordered)
        dep.linkDir = dep.checkout;

      std::optional<DepResolved> rootOpt;
      try
      {
        rootOpt = resolve_package_from_registry(specRaw);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, std::string("install failed: ") + ex.what());
        return 1;
      }

      if (!rootOpt)
      {
        vix::cli::util::err_line(std::cerr, "invalid package spec or package not found");
        return 1;
      }

      const auto finalIt = resolvedById.find(rootOpt->id);
      if (finalIt == resolvedById.end())
      {
        vix::cli::util::err_line(std::cerr, "resolved package disappeared from install graph");
        return 1;
      }

      DepResolved rootDep = finalIt->second;
      const std::string idDot = sanitize_id_dot(rootDep.id);
      const std::string buildLeaf = idDot + "-" + rootDep.version;
      const fs::path buildDir = global_build_dir() / buildLeaf;
      const fs::path stageDir = global_tmp_dir() / (buildLeaf + "-stage");

      std::error_code ec;
      fs::remove_all(stageDir, ec);
      fs::create_directories(stageDir, ec);
      if (ec)
      {
        vix::cli::util::err_line(std::cerr, "failed to create staging prefix: " + stageDir.string());
        return 1;
      }

      if (!fs::exists(rootDep.checkout / "CMakeLists.txt"))
      {
        vix::cli::util::err_line(std::cerr, "global install requires a CMakeLists.txt with install() rules: " + rootDep.id);
        return 1;
      }

      try
      {
        const std::vector<DepResolved> projectDeps = prepare_global_project_deps(rootDep, ordered);
        generate_cmake_in_project(rootDep.checkout, projectDeps);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, std::string("failed to prepare package dependencies: ") + ex.what());
        return 1;
      }

      const std::string cmake = "cmake";
      const std::string qSource = vix::cli::commands::helpers::quote(rootDep.checkout.string());
      const std::string qBuild = vix::cli::commands::helpers::quote(buildDir.string());
      const std::string qStage = vix::cli::commands::helpers::quote(stageDir.string());

      const std::string configureCmd =
          cmake + " -S " + qSource + " -B " + qBuild +
          " -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=" + qStage;
      if (run_checked_command(configureCmd, "cmake configure") != 0)
        return 1;

      const std::string buildCmd =
          cmake + " --build " + qBuild + " --config Release";
      if (run_checked_command(buildCmd, "cmake build") != 0)
        return 1;

      const std::string installCmd =
          cmake + " --install " + qBuild + " --config Release";
      if (run_checked_command(installCmd, "cmake install") != 0)
      {
        vix::cli::util::warn_line(std::cerr, "Global packages with executables must provide CMake install(TARGETS ... RUNTIME DESTINATION bin) rules.");
        return 1;
      }

      std::vector<GlobalExecutableDecl> declared;
      try
      {
        declared = read_declared_executables(rootDep.checkout / "vix.json");
        stage_built_executables_missing_from_install(rootDep, buildDir, stageDir, declared);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, ex.what());
        return 1;
      }

      std::vector<fs::path> files = collect_regular_files(stageDir);
      try
      {
        validate_staged_paths(files);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, ex.what());
        return 1;
      }

      if (files.empty())
      {
        try
        {
          copy_header_fallbacks_to_stage(rootDep, ordered, stageDir);
          files = collect_regular_files(stageDir);
          validate_staged_paths(files);
        }
        catch (const std::exception &ex)
        {
          vix::cli::util::err_line(std::cerr, std::string("install failed: ") + ex.what());
          return 1;
        }
      }

      if (files.empty())
      {
        vix::cli::util::err_line(std::cerr, "cmake install produced no files for: " + rootDep.id);
        return 1;
      }

      std::vector<std::string> commands = collect_installed_commands(stageDir);
      try
      {
        validate_declared_executables_installed(declared, commands);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, ex.what());
        return 1;
      }

      json registry;
      try
      {
        registry = load_global_manifest();
        ensure_no_global_conflicts(registry, rootDep.id, global_root_dir(), files);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, ex.what());
        return 1;
      }

      try
      {
        const json *previous = find_global_pkg(registry, rootDep.id);
        copy_staged_prefix(stageDir, global_root_dir(), files);
        if (previous)
          remove_obsolete_registered_files_for_package(*previous, global_root_dir(), files);
        const std::vector<std::string> shims = install_immediate_command_shims(registry, rootDep.id, commands);
        save_global_install(rootDep, rootDep.checkout, files, commands, shims);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, std::string("install failed: ") + ex.what());
        return 1;
      }

      fs::remove_all(stageDir, ec);

      ensure_user_path_contains_global_bin();

      vix::cli::util::ok_line(
          std::cout,
          rootDep.id + "@" + rootDep.version + " installed globally in " + format_elapsed(std::chrono::steady_clock::now() - started));

      return 0;
    }

    static json cmake_options_json(const std::vector<std::pair<std::string, std::string>> &options)
    {
      json obj = json::object();
      for (const auto &option : options)
        obj[option.first] = option.second;
      return obj;
    }

    static std::string requested_revision_text_from_parts(
        const std::string &tag,
        const std::string &branch,
        const std::string &rev)
    {
      if (!tag.empty())
        return tag;
      if (!branch.empty())
        return branch;
      if (!rev.empty())
        return rev;
      return "HEAD";
    }

    static void validate_git_dependency_paths_or_throw(const vix::cli::app::AppGitDependency &dep)
    {
      if (!dep.subdirectory.empty() && !safe_relative_path_string(dep.subdirectory))
        throw std::runtime_error("unsafe subdirectory for dependency " + dep.name + ": " + dep.subdirectory);

      for (const std::string &include : dep.includes)
      {
        if (!safe_relative_path_string(include))
          throw std::runtime_error("unsafe include path for dependency " + dep.name + ": " + include);
      }
    }

    static DepResolved resolve_app_git_dependency_or_throw(const vix::cli::app::AppGitDependency &appDep)
    {
      if (!is_safe_local_dep_name(appDep.name))
        throw std::runtime_error("invalid dependency name: " + appDep.name);
      if (!is_supported_git_url(appDep.git))
        throw std::runtime_error("unsupported Git URL: " + appDep.git);

      validate_git_dependency_paths_or_throw(appDep);

      std::string selectedTag = appDep.tag;
      if (selectedTag.empty() && appDep.branch.empty() && appDep.rev.empty())
        selectedTag = select_latest_stable_git_tag(appDep.git, false);

      const std::string commit = resolve_git_commit_or_throw(appDep.git, selectedTag, appDep.branch, appDep.rev);
      const fs::path checkout = clone_git_to_cache_or_throw(appDep.git, commit);
      const fs::path sourceDir = appDep.subdirectory.empty() ? checkout : (checkout / appDep.subdirectory);

      if (!fs::exists(sourceDir))
        throw std::runtime_error("subdirectory not found for dependency " + appDep.name + ": " + appDep.subdirectory);

      DepResolved dep;
      dep.id = appDep.name;
      dep.source = "git";
      dep.repo = appDep.git;
      dep.requested = requested_revision_text_from_parts(selectedTag, appDep.branch, appDep.rev);
      dep.version = commit.substr(0, std::min<std::size_t>(commit.size(), 12));
      dep.tag = selectedTag;
      dep.commit = commit;
      dep.checkout = checkout;
      dep.hash = vix::cli::util::sha256_package_directory(checkout).value_or("");
      dep.hashAlgorithm = vix::cli::util::PACKAGE_HASH_ALGORITHM;
      dep.hashVersion = vix::cli::util::PACKAGE_HASH_VERSION;
      dep.subdirectory = appDep.subdirectory;
      dep.headerOnly = appDep.headerOnly;
      dep.type = appDep.headerOnly ? "header-only" : "library";
      dep.includes = appDep.includes;
      if (dep.includes.empty() && appDep.headerOnly)
        dep.includes.push_back("include");
      if (!dep.includes.empty())
        dep.include = dep.includes.front();
      dep.cmakeTargets = appDep.targets;
      dep.cmakeOptions = app_cmake_options_to_pairs(appDep.cmakeOptions);

      const fs::path cmakeLists = sourceDir / "CMakeLists.txt";
      if (dep.headerOnly)
      {
        for (const std::string &include : dep.includes)
        {
          if (!fs::exists(sourceDir / include))
            throw std::runtime_error("include path not found for dependency " + dep.id + ": " + include);
        }
      }
      else if (fs::exists(cmakeLists))
      {
        if (dep.cmakeTargets.empty())
        {
          auto detected = detect_cmake_targets_from_file(cmakeLists);
          add_generated_alias_candidates(detected, cmakeLists, appDep.name);
          const auto chosen = choose_cmake_target(detected, appDep.name);
          if (chosen)
          {
            dep.cmakeTargets = {*chosen};
          }
          else if (!detected.empty())
          {
            std::ostringstream msg;
            msg << "Could not choose a CMake target automatically\n\nRepository exposes:";
            for (const auto &target : detected)
              msg << "\n  " << target;
            msg << "\n\nSelect one explicitly with --target <target>.";
            throw std::runtime_error(msg.str());
          }
        }
      }
      else
      {
        dep.includes = detect_header_include_roots(sourceDir);
        if (dep.includes.size() == 1)
        {
          dep.headerOnly = true;
          dep.type = "header-only";
          dep.include = dep.includes.front();
        }
        else if (dep.includes.size() > 1)
        {
          throw std::runtime_error("multiple header roots detected for dependency " + dep.id + "; use --header-only --include <path>");
        }
        else
        {
          throw std::runtime_error("The repository does not expose a supported CMake or header-only package: " + dep.id);
        }
      }

      return dep;
    }

    static json git_dependency_to_lock_json(const DepResolved &dep)
    {
      json item = json::object();
      item["id"] = dep.id;
      item["source"] = "git";
      item["url"] = dep.repo;
      item["repo"] = dep.repo;
      item["requested"] = dep.requested;
      item["version"] = dep.version;
      item["tag"] = dep.tag;
      item["commit"] = dep.commit;
      item["hash"] = dep.hash;
      item["hash_algorithm"] = dep.hashAlgorithm.empty() ? vix::cli::util::PACKAGE_HASH_ALGORITHM : dep.hashAlgorithm;
      item["hash_version"] = dep.hashVersion > 0 ? dep.hashVersion : vix::cli::util::PACKAGE_HASH_VERSION;
      item["subdirectory"] = dep.subdirectory;
      item["header_only"] = dep.headerOnly;
      item["includes"] = json::array();
      for (const std::string &include : dep.includes)
        item["includes"].push_back(include);
      item["targets"] = json::array();
      for (const std::string &target : dep.cmakeTargets)
        item["targets"].push_back(target);
      item["cmake_options"] = cmake_options_json(dep.cmakeOptions);
      return item;
    }

    static json read_lock_or_empty()
    {
      if (!fs::exists(lock_path()))
        return json{{"lockVersion", 1}, {"dependencies", json::array()}};
      json root = read_json_or_throw(lock_path());
      if (!root.is_object())
        root = json{{"lockVersion", 1}, {"dependencies", json::array()}};
      if (!root.contains("dependencies") || !root["dependencies"].is_array())
        root["dependencies"] = json::array();
      return root;
    }

    static void save_lock_json(const json &root)
    {
      write_json_or_throw(lock_path(), root);
    }

    static void upsert_lock_dependency(json &root, const json &dependency)
    {
      auto &arr = root["dependencies"];
      const std::string id = dependency.value("id", "");
      for (auto &item : arr)
      {
        if (item.is_object() && item.value("id", "") == id)
        {
          item = dependency;
          return;
        }
      }
      arr.push_back(dependency);
    }

    static std::vector<DepResolved> sync_vix_app_git_dependencies()
    {
      const fs::path appPath = fs::current_path() / "vix.app";
      if (!fs::exists(appPath))
        return {};

      const auto load = vix::cli::app::load_app_manifest(appPath);
      if (!load.success())
        throw std::runtime_error(load.error);

      std::vector<DepResolved> resolved;
      if (load.manifest.gitDependencies.empty())
        return resolved;

      json root = read_lock_or_empty();
      root["lockVersion"] = 1;

      for (const auto &appDep : load.manifest.gitDependencies)
      {
        DepResolved dep = resolve_app_git_dependency_or_throw(appDep);
        upsert_lock_dependency(root, git_dependency_to_lock_json(dep));
        resolved.push_back(dep);
      }

      std::sort(root["dependencies"].begin(), root["dependencies"].end(), [](const json &a, const json &b)
                { return a.value("id", "") < b.value("id", ""); });
      save_lock_json(root);
      return resolved;
    }

    static bool dependency_link_needs_update(
        const fs::path &link,
        const fs::path &expectedTarget)
    {
      std::error_code ec;

      const auto status = fs::symlink_status(link, ec);
      if (ec || status.type() == fs::file_type::not_found)
        return true;

#ifndef _WIN32
      if (fs::is_symlink(status))
      {
        fs::path actualTarget = fs::read_symlink(link, ec);
        if (ec)
          return true;

        if (actualTarget.is_relative())
          actualTarget = fs::absolute(link.parent_path() / actualTarget).lexically_normal();
        else
          actualTarget = fs::absolute(actualTarget).lexically_normal();

        const fs::path expected =
            fs::absolute(expectedTarget).lexically_normal();

        return actualTarget != expected;
      }
#endif

      return true;
    }

    static std::string read_text_file_or_empty_local(const fs::path &path)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        return {};
      std::ostringstream out;
      out << in.rdbuf();
      return out.str();
    }

    static std::string normalize_project_name_from_dir(std::string raw)
    {
      std::string out;
      bool dash = false;
      for (char c : raw)
      {
        const unsigned char ch = static_cast<unsigned char>(c);
        if (std::isalnum(ch))
        {
          out.push_back(static_cast<char>(std::tolower(ch)));
          dash = false;
        }
        else if (!dash && !out.empty())
        {
          out.push_back('-');
          dash = true;
        }
      }
      while (!out.empty() && out.back() == '-')
        out.pop_back();
      if (out.empty() || !std::isalpha(static_cast<unsigned char>(out.front())))
        out = "vix-project";
      return out;
    }

    static void create_minimal_vix_app_if_missing(const fs::path &appPath)
    {
      if (fs::exists(appPath))
        return;

      std::vector<std::string> sources;
      std::error_code ec;
      for (const auto &entry : fs::directory_iterator(fs::current_path(), ec))
      {
        if (ec || !entry.is_regular_file())
          continue;
        const fs::path p = entry.path();
        if (p.extension() == ".cpp" || p.extension() == ".cc" || p.extension() == ".cxx")
          sources.push_back(p.filename().string());
      }
      std::sort(sources.begin(), sources.end());

      std::ofstream out(appPath, std::ios::binary | std::ios::trunc);
      if (!out)
        throw std::runtime_error("cannot create vix.app");
      out << "name = \"" << normalize_project_name_from_dir(fs::current_path().filename().string()) << "\"\n";
      out << "type = \"executable\"\n";
      out << "standard = \"c++20\"\n";
      if (!sources.empty())
      {
        out << "sources = [";
        for (std::size_t i = 0; i < sources.size(); ++i)
        {
          if (i)
            out << ", ";
          out << "\"" << sources[i] << "\"";
        }
        out << "]\n";
      }
    }

    static void append_git_dependency_to_vix_app(const ParsedArgs &parsed)
    {
      const fs::path appPath = fs::current_path() / "vix.app";
      create_minimal_vix_app_if_missing(appPath);

      std::string name = trim_copy(parsed.gitName.empty() ? git_url_to_default_name(parsed.gitSpec) : parsed.gitName);
      if (!is_safe_local_dep_name(name))
        throw std::runtime_error("invalid dependency name: " + name);

      const std::string content = read_text_file_or_empty_local(appPath);
      const std::string section = "[dependencies." + name + "]";
      if (content.find(section) != std::string::npos)
        throw std::runtime_error("dependency already exists in vix.app: " + name);

      if (!parsed.gitSubdirectory.empty() && !safe_relative_path_string(parsed.gitSubdirectory))
        throw std::runtime_error("unsafe subdirectory: " + parsed.gitSubdirectory);
      if (!parsed.gitInclude.empty() && !safe_relative_path_string(parsed.gitInclude))
        throw std::runtime_error("unsafe include path: " + parsed.gitInclude);

      std::ofstream out(appPath, std::ios::app);
      if (!out)
        throw std::runtime_error("cannot update vix.app");

      out << "\n"
          << section << "\n";
      out << "git = \"" << parsed.gitSpec << "\"\n";
      if (!parsed.gitTag.empty())
        out << "tag = \"" << parsed.gitTag << "\"\n";
      if (!parsed.gitBranch.empty())
        out << "branch = \"" << parsed.gitBranch << "\"\n";
      if (!parsed.gitRev.empty())
        out << "rev = \"" << parsed.gitRev << "\"\n";
      if (!parsed.gitSubdirectory.empty())
        out << "subdirectory = \"" << parsed.gitSubdirectory << "\"\n";
      if (!parsed.gitTarget.empty())
        out << "target = \"" << parsed.gitTarget << "\"\n";
      if (parsed.gitHeaderOnly)
        out << "header_only = true\n";
      if (!parsed.gitInclude.empty())
        out << "include = \"" << parsed.gitInclude << "\"\n";
    }

    static void ensure_line_in_dependency_block(
        const fs::path &appPath,
        const std::string &name,
        const std::string &linePrefix,
        const std::string &line)
    {
      std::string content = read_text_file_or_empty_local(appPath);
      const std::string section = "[dependencies." + name + "]";
      const std::size_t start = content.find(section);
      if (start == std::string::npos)
        return;
      const std::size_t blockStart = content.find('\n', start);
      const std::size_t next = content.find("\n[", blockStart == std::string::npos ? start : blockStart);
      const std::size_t end = next == std::string::npos ? content.size() : next;
      const std::string block = content.substr(start, end - start);
      if (block.find(linePrefix) != std::string::npos)
        return;
      content.insert(end, "\n" + line);
      std::ofstream out(appPath, std::ios::binary | std::ios::trunc);
      out << content;
    }

    static void update_vix_app_with_detected_git_metadata(const std::vector<DepResolved> &resolved)
    {
      const fs::path appPath = fs::current_path() / "vix.app";
      for (const DepResolved &dep : resolved)
      {
        if (!dep.tag.empty())
          ensure_line_in_dependency_block(appPath, dep.id, "tag", "tag = \"" + dep.tag + "\"");
        if (!dep.cmakeTargets.empty())
          ensure_line_in_dependency_block(appPath, dep.id, "target", "target = \"" + dep.cmakeTargets.front() + "\"");
        if (dep.headerOnly)
        {
          ensure_line_in_dependency_block(appPath, dep.id, "header_only", "header_only = true");
          if (!dep.includes.empty())
            ensure_line_in_dependency_block(appPath, dep.id, "include", "include = \"" + dep.includes.front() + "\"");
        }
      }
    }

    static int install_git_dependency_from_cli(const ParsedArgs &parsed)
    {
      const auto started = std::chrono::steady_clock::now();
      if (!is_supported_git_url(parsed.gitSpec))
      {
        vix::cli::util::err_line(std::cerr, "unsupported Git URL: " + parsed.gitSpec);
        return 1;
      }

      const int revisionCount = (!parsed.gitTag.empty() ? 1 : 0) + (!parsed.gitBranch.empty() ? 1 : 0) + (!parsed.gitRev.empty() ? 1 : 0);
      if (revisionCount > 1)
      {
        vix::cli::util::err_line(std::cerr, "use only one of --tag, --branch, or --rev");
        return 1;
      }

      try
      {
        append_git_dependency_to_vix_app(parsed);
        const auto resolved = sync_vix_app_git_dependencies();
        update_vix_app_with_detected_git_metadata(resolved);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, ex.what());
        return 1;
      }

      const int rc = install_project_dependencies();
      if (rc != 0)
        return rc;

      const std::string name = trim_copy(parsed.gitName.empty() ? git_url_to_default_name(parsed.gitSpec) : parsed.gitName);
      vix::cli::util::ok_line(
          std::cout,
          name + " added from Git in " + format_elapsed(std::chrono::steady_clock::now() - started));
      return 0;
    }

    static int install_project_dependencies()
    {
      bool didWork = false;
      bool printedHeader = false;
      bool printedRefreshLine = false;
      bool lockChanged = false;
      std::size_t installedCount = 0;

      try
      {
        sync_vix_app_git_dependencies();
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, std::string("failed to resolve Git dependencies: ") + ex.what());
        return 1;
      }

      const fs::path lp = lock_path();

      if (!fs::exists(lp))
      {
        vix::cli::util::err_line(std::cerr, "missing vix.lock");
        vix::cli::util::warn_line(std::cerr, "Run: vix add @namespace/name[@version] or vix install <git-url>");
        return 1;
      }

      json lock;
      try
      {
        lock = read_json_or_throw(lp);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, std::string("failed to read vix.lock: ") + ex.what());
        return 1;
      }

      if (!lock.contains("dependencies") || !lock["dependencies"].is_array())
      {
        vix::cli::util::err_line(std::cerr, "invalid vix.lock: missing dependencies[]");
        return 1;
      }

      auto &depsArr = lock["dependencies"];
      if (depsArr.empty())
      {
        vix::cli::util::warn_line(std::cout, "No dependencies to install");
        return 0;
      }

      fs::create_directories(project_deps_dir());

      std::vector<DepResolved> resolved;
      resolved.reserve(depsArr.size());

      for (auto &d : depsArr)
      {
        DepResolved dep;
        try
        {
          dep = resolve_dep_from_lock_entry(d);
        }
        catch (const std::exception &ex)
        {
          vix::cli::util::err_line(std::cerr, ex.what());
          return 1;
        }

        const fs::path link = project_deps_dir() / sanitize_id_dot(dep.id);

        const bool checkoutExistedBefore = fs::exists(dep.checkout);
        const bool linkExistedBefore = fs::exists(link);
        const bool linkNeedsUpdate = dependency_link_needs_update(link, dep.checkout);

        if (!checkoutExistedBefore)
        {
          if (!printedHeader)
          {
            vix::cli::util::section(std::cout, "Installing dependencies");
            printedHeader = true;
          }

          if (dep.source == "git")
          {
            try
            {
              dep.checkout = clone_git_to_cache_or_throw(dep.repo, dep.commit);
            }
            catch (const std::exception &ex)
            {
              vix::cli::util::err_line(std::cerr, std::string("fetch failed: ") + dep.id);
              vix::cli::util::warn_line(std::cerr, ex.what());
              return 1;
            }
          }
          else
          {
            std::string outDir;
            const int rc = clone_checkout(dep.repo, sanitize_id_dot(dep.id), dep.commit, outDir);
            if (rc != 0)
            {
              vix::cli::util::err_line(std::cerr, "fetch failed: " + dep.id);
              vix::cli::util::warn_line(std::cerr, "Check git access, network, or re-add with a valid version.");
              return rc;
            }

            dep.checkout = fs::path(outDir);
          }
          didWork = true;
        }

        if (!verify_dependency_hash_or_refresh(
                dep,
                d,
                lockChanged,
                printedHeader,
                printedRefreshLine))
        {
          return 1;
        }

        if (dep.source != "git")
          load_dep_manifest(dep);

        try
        {
          ensure_symlink_or_copy_dir(dep.checkout, link);
        }
        catch (const std::exception &ex)
        {
          vix::cli::util::err_line(std::cerr, std::string("install failed: ") + ex.what());
          return 1;
        }

        if (!linkExistedBefore || linkNeedsUpdate)
        {
          didWork = true;
          installedCount++;
        }

        dep.linkDir = link;
        resolved.push_back(dep);

        if (!checkoutExistedBefore || !linkExistedBefore || linkNeedsUpdate)
        {
          if (!printedHeader)
          {
            vix::cli::util::section(std::cout, "Installing dependencies");
            printedHeader = true;
          }

          std::cout << "  " << CYAN << "•" << RESET << " "
                    << CYAN << BOLD << dep.id << RESET
                    << GRAY << "@" << RESET
                    << YELLOW << BOLD << dep.version << RESET
                    << "  "
                    << GRAY << "installed" << RESET
                    << "\n";
        }
      }

      if (lockChanged)
      {
        try
        {
          save_lock_json(lock);
        }
        catch (const std::exception &ex)
        {
          vix::cli::util::err_line(std::cerr, std::string("failed to write vix.lock: ") + ex.what());
          return 1;
        }
        didWork = true;
      }

      const bool cmakeExistedBefore = fs::exists(project_deps_cmake());

      try
      {
        auto ordered = sort_deps_topologically(resolved);
        generate_cmake(ordered);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, std::string("failed to generate CMake integration: ") + ex.what());
        return 1;
      }

      if (!cmakeExistedBefore)
        didWork = true;

      if (!didWork)
      {
        vix::cli::util::ok_line(std::cout, "Dependencies already up to date");
        return 0;
      }

      print_next_steps(
          installedCount,
          resolved.size(),
          true);

      return 0;
    }

  } // namespace

  int InstallCommand::run(const std::vector<std::string> &args)
  {
    ParsedArgs parsed;
    try
    {
      parsed = parse_args(args);
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(std::cerr, ex.what());
      return 1;
    }

    if (parsed.globalMode)
    {
      if (parsed.globalSpec.empty())
      {
        vix::cli::util::err_line(std::cerr, "missing package after -g");
        vix::cli::util::warn_line(std::cerr, "Example: vix install -g @gk/jwt@1.0.0");
        return 1;
      }

      return install_global_package(parsed.globalSpec);
    }

    if (!parsed.gitSpec.empty())
      return install_git_dependency_from_cli(parsed);

    return install_project_dependencies();
  }

  int InstallCommand::help()
  {
    std::cout
        << "vix install\n"
        << "Install project dependencies from vix.lock or install one global package.\n\n"

        << "Usage\n"
        << "  vix install\n"
        << "  vix install <git-url> [options]\n"
        << "  vix install -g [@]namespace/name[@version]\n\n"

        << "Examples\n"
        << "  vix install\n"
        << "  vix install https://github.com/fmtlib/fmt\n"
        << "  vix install https://github.com/fmtlib/fmt@11.2.0\n"
        << "  vix install https://github.com/nlohmann/json.git --tag v3.12.0 --target nlohmann_json::nlohmann_json\n"
        << "  vix install https://github.com/example/headers --header-only --include include\n"
        << "  vix install git@github.com:company/repo.git --name parser --subdirectory libs/parser --target company::parser\n"
        << "  vix install -g gk/jwt\n"
        << "  vix install -g gk/jwt@^1.0.0\n"
        << "  vix install -g @gk/jwt\n"
        << "  vix install -g @gk/jwt@~1.2.0\n\n"

        << "What happens\n"
        << "  • Project mode:\n"
        << "    - Reads exact resolved dependencies from vix.lock\n"
        << "    - Reuses cached packages when available\n"
        << "    - Installs dependencies into ./.vix/deps/\n"
        << "    - Generates ./.vix/vix_deps.cmake for CMake projects\n"
        << "\n"
        << "  • Git dependency mode:\n"
        << "    - Adds a [dependencies.<name>] block to vix.app\n"
        << "    - Auto-selects the newest stable SemVer tag when no revision is provided\n"
        << "    - Auto-detects the main CMake target when possible\n"
        << "    - Resolves tag, branch, rev, or HEAD to an exact commit\n"
        << "    - Stores the checkout in ~/.vix/cache/git/ and links it into ./.vix/deps/\n"
        << "    - Supports CMake repositories and header-only repositories\n"
        << "\n"
        << "  • Global mode (-g):\n"
        << "    - Resolves a package from the registry\n"
        << "    - Builds it with CMake in Release mode\n"
        << "    - Runs cmake --install into a Vix-managed user prefix\n"
        << "    - Records installed files and commands in ~/.vix/global/installed.json\n\n"

        << "Git options\n"
        << "  --name <name>           Local dependency name in vix.app\n"
        << "  --tag <tag>             Resolve a Git tag; also supports <url>@<tag>\n"
        << "  --branch <branch>       Resolve a Git branch to a commit\n"
        << "  --rev <commit>          Use a commit/revision\n"
        << "  --target <target>       CMake target to link, e.g. fmt::fmt\n"
        << "  --subdirectory <dir>    CMake project inside a monorepo\n"
        << "  --header-only           Treat the repository as headers only\n"
        << "  --include <dir>         Include directory for header-only deps\n\n"

        << "Project outputs\n"
        << "  ./.vix/deps/\n"
        << "  ./.vix/vix_deps.cmake\n\n"

        << "Global outputs\n"
        << "  ~/.vix/global/bin/\n"
        << "  ~/.vix/global/include/\n"
        << "  ~/.vix/global/lib/\n"
        << "  ~/.vix/global/installed.json\n\n"

        << "Notes\n"
        << "  • Project install is strict and reproducible\n"
        << "  • Project install does not resolve dependency ranges from vix.json\n"
        << "  • Use 'vix add' or 'vix update' to change resolved versions\n"
        << "  • Use 'vix registry sync' if a package is not found\n"
        << "  • '@namespace/name' is supported\n";

    return 0;
  }
}
