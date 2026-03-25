/**
 *
 *  @file DepsCommand.cpp
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
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/util/Shell.hpp>
#include <vix/cli/util/Hash.hpp>
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
      std::string globalSpec;
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

    struct DepResolved
    {
      std::string id;
      std::string version;
      std::string repo;
      std::string tag;
      std::string commit;
      std::string hash;

      std::string type;
      std::string include{"include"};
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

    static std::string to_lower(std::string s)
    {
      std::transform(
          s.begin(),
          s.end(),
          s.begin(),
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

    static fs::path store_checkout_path(const std::string &id, const std::string &commit)
    {
      return store_git_dir() / sanitize_id_dot(id) / commit;
    }

    static fs::path global_pkg_dir(const std::string &id, const std::string &commit)
    {
      return global_pkgs_dir() / sanitize_id_dot(id) / commit;
    }

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
        vix::cli::util::err_line(
            std::cerr,
            "no versions available for: " + spec.ns + "/" + spec.name);
        return 1;
      }

      spec.resolvedVersion = latest;
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
      if (fs::exists(p, ec))
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
        else if (parsed.globalMode && parsed.globalSpec.empty())
        {
          parsed.globalSpec = arg;
        }
      }

      return parsed;
    }

    static bool verify_dependency_hash(const DepResolved &dep)
    {
      if (dep.hash.empty())
        return true;

      const auto actualHashOpt = vix::cli::util::sha256_directory(dep.checkout);
      if (!actualHashOpt)
      {
        vix::cli::util::warn_line(std::cerr, "could not compute hash for: " + dep.id);
        return true;
      }

      if (*actualHashOpt != dep.hash)
      {
        vix::cli::util::err_line(std::cerr, "integrity check failed: " + dep.id);
        vix::cli::util::err_line(std::cerr, "expected: " + dep.hash);
        vix::cli::util::err_line(std::cerr, "actual:   " + *actualHashOpt);
        return false;
      }

      return true;
    }

    static void load_dep_manifest(DepResolved &dep)
    {
      const fs::path manifest = dep.checkout / "vix.json";
      if (!fs::exists(manifest))
      {
        dep.type = "unknown";
        dep.include = "include";
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
    }

    static DepResolved resolve_dep_from_lock_entry(const json &d)
    {
      DepResolved dep;
      dep.id = d.value("id", "");
      dep.version = d.value("version", "");
      dep.repo = d.value("repo", "");
      dep.tag = d.value("tag", "");
      dep.commit = d.value("commit", "");
      dep.hash = d.value("hash", "");

      if (dep.id.empty() || dep.repo.empty() || dep.commit.empty())
        throw std::runtime_error("invalid dependency entry in vix.lock (missing id/repo/commit)");

      dep.checkout = store_checkout_path(dep.id, dep.commit);
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
        throw std::runtime_error(
            "version not found: " + spec.id() + "@" + spec.resolvedVersion);

      const json v = versions.at(spec.resolvedVersion);

      DepResolved dep;
      dep.id = spec.id();
      dep.version = spec.resolvedVersion;
      dep.repo = entry.at("repo").at("url").get<std::string>();
      dep.tag = v.at("tag").get<std::string>();
      dep.commit = v.at("commit").get<std::string>();
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
      if (!root.contains("packages") || !root["packages"].is_array())
        root["packages"] = json::array();

      return root;
    }

    static void save_global_manifest(const json &root)
    {
      fs::create_directories(global_root_dir());
      write_json_or_throw(global_manifest_path(), root);
    }

    static void save_global_install(const DepResolved &dep, const fs::path &installedPath)
    {
      json root = load_global_manifest();
      auto &arr = root["packages"];

      bool updated = false;

      for (auto &item : arr)
      {
        if (item.value("id", "") == dep.id)
        {
          item["id"] = dep.id;
          item["version"] = dep.version;
          item["repo"] = dep.repo;
          item["tag"] = dep.tag;
          item["commit"] = dep.commit;
          item["hash"] = dep.hash;
          item["type"] = dep.type;
          item["include"] = dep.include;
          item["installed_path"] = installedPath.string();
          updated = true;
          break;
        }
      }

      if (!updated)
      {
        arr.push_back({
            {"id", dep.id},
            {"version", dep.version},
            {"repo", dep.repo},
            {"tag", dep.tag},
            {"commit", dep.commit},
            {"hash", dep.hash},
            {"type", dep.type},
            {"include", dep.include},
            {"installed_path", installedPath.string()},
        });
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

      for (const auto &dep : deps)
      {
        const std::string safe = cmake_safe_target(dep.id);
        const std::string alias = cmake_alias_target(dep.id);

        out << "# " << dep.id << " @" << dep.version << " (" << dep.commit << ")\n";

        if (dep.type == "header-only" || dep.type == "header_only" || dep.type == "headers")
        {
          const fs::path inc = dep.linkDir / dep.include;
          out << "add_library(" << safe << " INTERFACE)\n";
          out << "add_library(" << alias << " ALIAS " << safe << ")\n";
          out << "target_include_directories(" << safe << " INTERFACE "
              << cmake_quote(inc.string()) << ")\n";
        }
        else if (dep.type == "library" ||
                 dep.type == "header-and-source" ||
                 dep.type == "header_and_source" ||
                 dep.type == "headers-and-sources")
        {
          const fs::path depSourceDir = dep.linkDir;
          const std::string buildDirName = "_vix_build_" + sanitize_id_dot(dep.id);

          out << "if(NOT TARGET " << alias << ")\n";
          out << "  add_subdirectory("
              << cmake_quote(depSourceDir.string()) << " "
              << cmake_quote((project_vix_dir() / buildDirName).string()) << ")\n";
          out << "endif()\n";

          out << "if(NOT TARGET " << alias << ")\n";
          out << "  message(FATAL_ERROR "
              << cmake_quote("Dependency " + dep.id + " did not define expected target " + alias)
              << ")\n";
          out << "endif()\n";
        }
        else
        {
          out << "message(WARNING "
              << cmake_quote("Unsupported Vix package type for " + dep.id + ": " + dep.type)
              << ")\n";
        }

        out << "\n";
      }
    }

    static void print_next_steps(const std::vector<DepResolved> &deps)
    {
      vix::cli::util::one_line_spacer(std::cout);

      vix::cli::util::ok_line(std::cout, "Dependencies ready");
      vix::cli::util::info(std::cout, std::to_string(deps.size()) + " package(s) installed");
      vix::cli::util::info(std::cout, "CMake integration generated");
      std::cout << "\n";

      vix::cli::util::warn_line(std::cout, "Next:");
      std::cout << "  " << GRAY << "• " << RESET
                << "Add this to your "
                << CYAN << BOLD << "CMakeLists.txt" << RESET
                << ":\n\n";

      std::cout << "    include(.vix/vix_deps.cmake)\n";
      std::cout << "    add_executable(app main.cpp)\n";
      std::cout << "    target_link_libraries(app PRIVATE";

      for (const auto &d : deps)
        std::cout << " " << cmake_alias_target(d.id);

      std::cout << ")\n\n";
    }

    static int install_global_package(const std::string &specRaw)
    {
      if (ensure_registry_present() != 0)
        return 1;

      std::optional<DepResolved> resolvedOpt;
      try
      {
        resolvedOpt = resolve_package_from_registry(specRaw);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, std::string("install failed: ") + ex.what());
        return 1;
      }

      if (!resolvedOpt)
      {
        vix::cli::util::err_line(std::cerr, "invalid package spec or package not found");
        vix::cli::util::warn_line(std::cerr, "Expected: @namespace/name[@version]");
        vix::cli::util::warn_line(std::cerr, "Example: vix install -g @gk/jwt@1.0.0");
        return 1;
      }

      DepResolved dep = *resolvedOpt;

      fs::create_directories(global_pkgs_dir());

      const bool checkoutExistedBefore = fs::exists(dep.checkout);
      if (!checkoutExistedBefore)
      {
        vix::cli::util::section(std::cout, "Installing global package");
        vix::cli::util::one_line_spacer(std::cout);

        std::string outDir;
        const int rc = clone_checkout(dep.repo, sanitize_id_dot(dep.id), dep.commit, outDir);
        if (rc != 0)
        {
          vix::cli::util::err_line(std::cerr, "fetch failed: " + dep.id);
          vix::cli::util::warn_line(std::cerr, "Check git access, network, or registry metadata.");
          return rc;
        }

        dep.checkout = fs::path(outDir);
      }

      dep.hash = vix::cli::util::sha256_directory(dep.checkout).value_or("");
      if (!verify_dependency_hash(dep))
      {
        vix::cli::util::warn_line(std::cerr, "The cached checkout appears modified or corrupt.");
        vix::cli::util::warn_line(std::cerr, "Try: vix store gc");
        return 1;
      }

      load_dep_manifest(dep);

      const fs::path dst = global_pkg_dir(dep.id, dep.commit);
      const bool linkExistedBefore = fs::exists(dst);

      try
      {
        ensure_symlink_or_copy_dir(dep.checkout, dst);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, std::string("install failed: ") + ex.what());
        return 1;
      }

      dep.linkDir = dst;
      save_global_install(dep, dst);

      if (!checkoutExistedBefore || !linkExistedBefore)
      {
        if (checkoutExistedBefore)
        {
          vix::cli::util::section(std::cout, "Installing global package");
          vix::cli::util::one_line_spacer(std::cout);
        }

        std::cout << "  " << CYAN << "•" << RESET << " "
                  << CYAN << BOLD << dep.id << RESET
                  << GRAY << "@" << RESET
                  << YELLOW << BOLD << dep.version << RESET
                  << "  "
                  << GRAY << "installed globally" << RESET
                  << "\n";
      }
      else
      {
        vix::cli::util::ok_line(std::cout, "Global package already up to date");
      }

      vix::cli::util::one_line_spacer(std::cout);
      vix::cli::util::ok_line(std::cout, "Global package ready");
      vix::cli::util::info(std::cout, "Installed into: " + dst.string());
      vix::cli::util::info(std::cout, "Manifest updated: " + global_manifest_path().string());

      return 0;
    }

    static int install_project_dependencies()
    {
      bool didWork = false;
      bool printedHeader = false;

      const fs::path lp = lock_path();

      if (!fs::exists(lp))
      {
        vix::cli::util::err_line(std::cerr, "missing vix.lock");
        vix::cli::util::warn_line(std::cerr, "Run: vix add @namespace/name[@version]");
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

      const auto &depsArr = lock["dependencies"];
      if (depsArr.empty())
      {
        vix::cli::util::warn_line(std::cout, "No dependencies to install");
        return 0;
      }

      fs::create_directories(project_deps_dir());

      std::vector<DepResolved> resolved;
      resolved.reserve(depsArr.size());

      for (const auto &d : depsArr)
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

        if (!checkoutExistedBefore)
        {
          if (!printedHeader)
          {
            vix::cli::util::section(std::cout, "Installing dependencies");
            vix::cli::util::one_line_spacer(std::cout);
            printedHeader = true;
          }

          std::string outDir;
          const int rc = clone_checkout(dep.repo, sanitize_id_dot(dep.id), dep.commit, outDir);
          if (rc != 0)
          {
            vix::cli::util::err_line(std::cerr, "fetch failed: " + dep.id);
            vix::cli::util::warn_line(std::cerr, "Check git access, network, or re-add with a valid version.");
            return rc;
          }

          dep.checkout = fs::path(outDir);
          didWork = true;
        }

        if (!verify_dependency_hash(dep))
        {
          vix::cli::util::warn_line(std::cerr, "The cached checkout appears modified or corrupt.");
          vix::cli::util::warn_line(std::cerr, "Try: vix store gc && vix install");
          return 1;
        }

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

        if (!linkExistedBefore)
          didWork = true;

        dep.linkDir = link;
        resolved.push_back(dep);

        if (!checkoutExistedBefore || !linkExistedBefore)
        {
          if (!printedHeader)
          {
            vix::cli::util::section(std::cout, "Installing dependencies");
            vix::cli::util::one_line_spacer(std::cout);
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

      const bool cmakeExistedBefore = fs::exists(project_deps_cmake());

      try
      {
        generate_cmake(resolved);
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

      print_next_steps(resolved);
      return 0;
    }

  } // namespace

  int InstallCommand::run(const std::vector<std::string> &args)
  {
    const ParsedArgs parsed = parse_args(args);

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

    return install_project_dependencies();
  }

  int InstallCommand::help()
  {
    std::cout
        << "vix install\n"
        << "Install project dependencies or one global package.\n\n"

        << "Usage\n"
        << "  vix install\n"
        << "  vix install -g [@]namespace/name[@version]\n\n"

        << "Examples\n"
        << "  vix install\n"
        << "  vix install -g gk/jwt\n"
        << "  vix install -g gk/jwt@1.0.0\n"
        << "  vix install -g @gk/jwt\n"
        << "  vix install -g @gk/jwt@1.0.0\n\n"

        << "What happens\n"
        << "  • Installs dependencies pinned in vix.lock\n"
        << "  • Reuses cached packages when available\n"
        << "  • Generates ./.vix/vix_deps.cmake for CMake projects\n"
        << "  • Supports global installs with -g\n\n"

        << "Project outputs\n"
        << "  ./.vix/deps/\n"
        << "  ./.vix/vix_deps.cmake\n\n"

        << "Global outputs\n"
        << "  ~/.vix/global/packages/\n"
        << "  ~/.vix/global/installed.json\n\n"

        << "Notes\n"
        << "  • Use 'vix registry sync' if a package is not found\n"
        << "  • '@namespace/name' is supported\n"
        << "  • 'vix deps' may remain as a compatibility alias internally\n";

    return 0;
  }
}
