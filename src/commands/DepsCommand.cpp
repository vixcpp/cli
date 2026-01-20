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
#include <vix/cli/commands/DepsCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/util/Shell.hpp>
#include <vix/cli/Style.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    static std::string home_dir()
    {
#ifdef _WIN32
      const char *home = std::getenv("USERPROFILE");
#else
      const char *home = std::getenv("HOME");
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

    static json read_json_or_throw(const fs::path &p)
    {
      std::ifstream in(p);
      if (!in)
        throw std::runtime_error("cannot open: " + p.string());
      json j;
      in >> j;
      return j;
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
      // "gaspardkirira/tree" -> "vix__gaspardkirira__tree"
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
      // "gaspardkirira/tree" -> "gaspardkirira::tree"
      const auto slash = id.find('/');
      if (slash == std::string::npos)
        return id;
      return id.substr(0, slash) + "::" + id.substr(slash + 1);
    }

    static fs::path store_checkout_path(const std::string &id, const std::string &commit)
    {
      const std::string idDot = sanitize_id_dot(id);
      return store_git_dir() / idDot / commit;
    }

    static int ensure_checkout_present(
        const std::string &repoUrl,
        const std::string &id,
        const std::string &commit)
    {
      fs::create_directories(store_git_dir());

      const fs::path dst = store_checkout_path(id, commit);
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
            "git -C " + dst.string() + " -c advice.detachedHead=false checkout -q " + commit;
        const int rc = vix::cli::util::run_cmd_retry_debug(cmd);
        if (rc != 0)
          return rc;
      }

      return 0;
    }

    static void remove_if_exists(const fs::path &p)
    {
      std::error_code ec;
      if (fs::exists(p, ec))
        fs::remove(p, ec);
    }

    static void ensure_symlink_or_copy_dir(const fs::path &src, const fs::path &dst)
    {
      std::error_code ec;
      remove_if_exists(dst);

#ifdef _WIN32
      fs::create_directories(dst, ec);
      fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
#else
      fs::create_directories(dst.parent_path(), ec);
      fs::create_directory_symlink(src, dst, ec);
      if (ec)
      {
        ec.clear();
        fs::create_directories(dst, ec);
        fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (ec)
          throw std::runtime_error("failed to link/copy dependency: " + dst.string());
      }
#endif
    }

    struct DepResolved
    {
      std::string id;
      std::string version;
      std::string repo;
      std::string tag;
      std::string commit;

      std::string type;    // "header-only" etc.
      std::string include; // include folder for header-only
      fs::path checkout;
      fs::path linkDir;
    };

    static DepResolved resolve_dep_from_lock_entry(const json &d)
    {
      DepResolved dep;
      dep.id = d.value("id", "");
      dep.version = d.value("version", "");
      dep.repo = d.value("repo", "");
      dep.tag = d.value("tag", "");
      dep.commit = d.value("commit", "");

      if (dep.id.empty() || dep.repo.empty() || dep.commit.empty())
        throw std::runtime_error("invalid dependency entry in vix.lock (missing id/repo/commit)");

      dep.checkout = store_checkout_path(dep.id, dep.commit);
      return dep;
    }

    static void load_dep_manifest(DepResolved &dep)
    {
      const fs::path manifest = dep.checkout / "vix.json";
      if (!fs::exists(manifest))
      {
        dep.type = "unknown";
        dep.include = "";
        return;
      }

      const json j = read_json_or_throw(manifest);

      dep.type = j.value("type", "");
      if (dep.type.empty())
      {
        dep.type = "header-only";
      }

      if (j.contains("include") && j["include"].is_string())
        dep.include = j["include"].get<std::string>();
      else
        dep.include = "include";
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
      out << "# Generated by: vix deps\n";
      out << "# DO NOT EDIT MANUALLY\n";
      out << "# ======================================================\n\n";

      out << "set(_VIX_DEPS_DIR " << cmake_quote(project_deps_dir().string()) << ")\n\n";

      for (const auto &dep : deps)
      {
        const std::string safe = cmake_safe_target(dep.id);
        const std::string alias = cmake_alias_target(dep.id);

        out << "# " << dep.id << " @" << dep.version << " (" << dep.commit << ")\n";
        out << "add_library(" << safe << " INTERFACE)\n";
        out << "add_library(" << alias << " ALIAS " << safe << ")\n";

        if (dep.type == "header-only" || dep.type == "header_only" || dep.type == "headers")
        {
          const fs::path inc = dep.linkDir / dep.include;
          out << "target_include_directories(" << safe << " INTERFACE " << cmake_quote(inc.string()) << ")\n";
        }
        else
        {
          out << "# NOTE: non header-only package type not installed automatically in V1\n";
        }

        out << "\n";
      }
    }

    static void print_next_steps()
    {
      vix::cli::util::one_line_spacer(std::cout);
      vix::cli::util::ok_line(std::cout, "deps installed + CMake generated");
      vix::cli::util::kv(std::cout, "cmake", project_deps_cmake().string());
      std::cout << "\n";

      vix::cli::util::warn_line(std::cout, "Next steps (CMake):");
      std::cout << "  " << GRAY << "• " << RESET
                << "Add to your CMakeLists.txt:\n\n";

      std::cout << "    include(.vix/vix_deps.cmake)\n";
      std::cout << "    add_executable(app main.cpp)\n";
      std::cout << "    target_link_libraries(app PRIVATE gaspardkirira::tree gaspardkirira::binary_search)\n\n";
    }

  }

  int DepsCommand::run(const std::vector<std::string> &args)
  {
    (void)args;

    vix::cli::util::section(std::cout, "Deps");

    const fs::path lp = lock_path();
    vix::cli::util::kv(std::cout, "lock", lp.string());

    if (!fs::exists(lp))
    {
      vix::cli::util::err_line(std::cerr, "missing vix.lock");
      vix::cli::util::warn_line(std::cerr, "Run: vix add <namespace>/<name>@<version>");
      return 1;
    }

    const json lock = read_json_or_throw(lp);

    if (!lock.contains("dependencies") || !lock["dependencies"].is_array())
    {
      vix::cli::util::err_line(std::cerr, "invalid vix.lock: missing dependencies[]");
      return 1;
    }

    const auto depsArr = lock["dependencies"];
    if (depsArr.empty())
    {
      vix::cli::util::warn_line(std::cout, "No dependencies in vix.lock");
      return 0;
    }

    fs::create_directories(project_deps_dir());

    std::vector<DepResolved> resolved;
    resolved.reserve(depsArr.size());

    for (const auto &d : depsArr)
    {
      DepResolved dep = resolve_dep_from_lock_entry(d);

      vix::cli::util::kv(std::cout, "dep", dep.id + "@" + dep.version);

      if (!fs::exists(dep.checkout))
      {
        vix::cli::util::warn_line(std::cout, "fetching: " + dep.repo);
        const int rc = ensure_checkout_present(dep.repo, dep.id, dep.commit);
        if (rc != 0)
        {
          vix::cli::util::err_line(std::cerr, "fetch failed for: " + dep.id);
          vix::cli::util::warn_line(std::cerr, "Check git access/network, or re-add/publish with a valid commit.");
          return rc;
        }
      }

      load_dep_manifest(dep);

      // Create a stable project-local link:
      // .vix/deps/<ns>.<name> -> ~/.vix/store/git/<ns>.<name>/<commit>
      const fs::path link = project_deps_dir() / sanitize_id_dot(dep.id);
      ensure_symlink_or_copy_dir(dep.checkout, link);
      dep.linkDir = link;

      resolved.push_back(std::move(dep));
    }

    generate_cmake(resolved);
    print_next_steps();
    return 0;
  }

  int DepsCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix deps\n\n"
        << "Description:\n"
        << "  Install project dependencies from vix.lock into ./.vix/deps and generate ./.vix/vix_deps.cmake\n\n"
        << "Example:\n"
        << "  vix add gaspardkirira/tree@0.4.0\n"
        << "  vix add gaspardkirira/binary_search@0.1.1\n"
        << "  vix deps\n";
    return 0;
  }
}
