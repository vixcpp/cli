/**
 *
 *  @file OrmCommand.cpp
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
#include <vix/cli/commands/OrmCommand.hpp>
#include <iostream>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
  static const char *env_or(const char *k, const char *defv)
  {
    if (const char *v = std::getenv(k))
      return v;
    return defv;
  }

  static std::string shell_quote(const std::string &s)
  {
    std::string out = "'";
    for (char c : s)
    {
      if (c == '\'')
        out += "'\\''";
      else
        out += c;
    }
    out += "'";
    return out;
  }

  static fs::path get_exe_path()
  {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
      return {};
    return fs::path(buf);

#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string tmp(size, '\0');
    if (_NSGetExecutablePath(tmp.data(), &size) != 0)
      return {};
    fs::path p = fs::path(tmp.c_str());

    // resolve symlinks
    std::error_code ec;
    p = fs::weakly_canonical(p, ec);
    return ec ? fs::path(tmp.c_str()) : p;

#else
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec || exe.empty())
      return {};
    return exe;
#endif
  }

  static fs::path get_exe_dir()
  {
    fs::path exe = get_exe_path();
    if (exe.empty())
      return {};
    return exe.parent_path();
  }

  static std::string find_migrator_tool()
  {
    // New preferred env var
    if (const char *t = std::getenv("VIX_DB_TOOL"))
      return std::string(t);

    // Backward compat (old name)
    if (const char *t = std::getenv("VIX_ORM_TOOL"))
      return std::string(t);

    const auto try_paths = [](const std::vector<fs::path> &paths) -> std::string
    {
      for (const auto &p : paths)
      {
        if (!p.empty() && fs::exists(p))
          return p.string();
      }
      return {};
    };

    if (fs::path binDir = get_exe_dir(); !binDir.empty())
    {
      fs::path prefix = binDir.parent_path();

      // Installed layout candidates (vix_db_migrator)
      std::vector<fs::path> installed = {
          // your current install rule: ${CMAKE_INSTALL_LIBEXECDIR}/vix
          prefix / "libexec" / "vix" / "vix_db_migrator",

          // some distros / variants
          prefix / "lib" / "vix" / "libexec" / "vix_db_migrator",
          prefix / "lib" / "vix" / "vix_db_migrator",

          // same dir as the CLI binary (dev installs)
          binDir / "vix_db_migrator",

          // Backward compat for older installs
          prefix / "libexec" / "vix" / "vix_orm_migrator",
          prefix / "lib" / "vix" / "libexec" / "vix_orm_migrator",
          prefix / "lib" / "vix" / "vix_orm_migrator",
          binDir / "vix_orm_migrator",
      };

      if (auto found = try_paths(installed); !found.empty())
        return found;
    }

    // Dev build candidates (umbrella + module builds)
    std::vector<fs::path> dev = {
        // umbrella builds
        fs::path("build/db_build/vix_db_migrator"),
        fs::path("build/db_build/vix_db_migrator.exe"),

        // module-local builds (varies by generator)
        fs::path("modules/db/build/vix_db_migrator"),
        fs::path("modules/db/build/vix_db_migrator.exe"),
        fs::path("modules/db/build-ninja/vix_db_migrator"),
        fs::path("modules/db/build-ninja/vix_db_migrator.exe"),

        // relative when running from modules/cli
        fs::path("../build/db_build/vix_db_migrator"),
        fs::path("../build/db_build/vix_db_migrator.exe"),
        fs::path("../db/build/vix_db_migrator"),
        fs::path("../db/build/vix_db_migrator.exe"),
        fs::path("../db/build-ninja/vix_db_migrator"),
        fs::path("../db/build-ninja/vix_db_migrator.exe"),

        // Backward compat dev paths (old orm tool)
        fs::path("build/orm_build/vix_orm_migrator"),
        fs::path("modules/orm/build/vix_orm_migrator"),
        fs::path("../build/orm_build/vix_orm_migrator"),
        fs::path("../orm/build/vix_orm_migrator"),
    };

    if (auto found = try_paths(dev); !found.empty())
      return found;

    // Final fallback: prefer new name, but keep old for legacy systems
    return "vix_db_migrator";
  }

  static std::string get_flag(const std::vector<std::string> &args,
                              const std::string &key)
  {
    for (size_t i = 0; i + 1 < args.size(); ++i)
      if (args[i] == key)
        return args[i + 1];
    return {};
  }

  static bool looks_like_vix_repo_root(const fs::path &p)
  {
    return fs::exists(p / "modules") &&
           fs::is_directory(p / "modules") &&
           fs::exists(p / "CMakeLists.txt");
  }
  static bool looks_like_app_root(const fs::path &p)
  {
    if (fs::exists(p / "CMakePresets.json"))
      return true;
    if (fs::exists(p / "src") && fs::is_directory(p / "src") && fs::exists(p / "CMakeLists.txt"))
      return true;

    return false;
  }

  static fs::path find_project_root(fs::path p)
  {
    fs::path best;
    fs::path bestVix;

    for (int depth = 0; depth < 12; ++depth)
    {
      if (looks_like_vix_repo_root(p))
        bestVix = p;

      if (looks_like_app_root(p))
        best = p;

      if (best.empty() && fs::exists(p / "CMakeLists.txt"))
        best = p;

      if (!p.has_parent_path())
        break;
      fs::path parent = p.parent_path();
      if (parent == p)
        break;
      p = parent;
    }

    if (!bestVix.empty())
      return bestVix;
    if (!best.empty())
      return best;
    return {};
  }

  static fs::path detect_migrations_dir(const fs::path &projectDir)
  {
    if (looks_like_vix_repo_root(projectDir))
      return {};

    std::vector<fs::path> candidates = {
        projectDir / "migrations",
        projectDir / "db" / "migrations",
        projectDir / "database" / "migrations",

        projectDir / "sql" / "migrations",
        projectDir / "db" / "sql",
        projectDir / "migrations" / "mysql",
        projectDir / "migrations" / "sqlite"};

    for (const auto &c : candidates)
    {
      if (fs::exists(c) && fs::is_directory(c))
        return c;
    }

    return {};
  }
}

namespace vix::commands::OrmCommand
{
  int run(const std::vector<std::string> &args)
  {
    if (args.empty() || args[0] == "-h" || args[0] == "--help")
      return help();

    const std::string sub = args[0];
    if (sub != "migrate" && sub != "rollback" && sub != "status" && sub != "makemigrations")
    {
      std::cerr << "vix: unknown orm subcommand '" << sub << "'\n\n";
      help();
      return 1;
    }

    const bool is_make = (sub == "makemigrations");

    auto parse_opt = [&](const std::string &key) -> std::string
    {
      // --key value
      {
        const std::string v = get_flag(args, key);
        if (!v.empty())
          return v;
      }

      // --key=value
      const std::string prefix = key + "=";
      for (const auto &a : args)
        if (a.rfind(prefix, 0) == 0)
          return a.substr(prefix.size());

      return {};
    };

    auto flag_or_env = [&](const std::string &flag,
                           const char *envKey,
                           const char *defv) -> std::string
    {
      const std::string v = get_flag(args, flag);
      if (!v.empty())
        return v;
      return env_or(envKey, defv);
    };

    // Project root
    fs::path projectDir;
    const std::string projFlag = get_flag(args, "--project-dir");
    if (!projFlag.empty())
      projectDir = fs::path(projFlag);
    else
      projectDir = find_project_root(fs::current_path());

    if (projectDir.empty())
    {
      std::cerr << "vix orm: unable to detect project directory.\n";
      std::cerr << "Tip: use --project-dir <path>\n";
      return 1;
    }

    // Detect/resolve migrations dir
    fs::path migDir;
    const std::string dirFlag = get_flag(args, "--dir");
    if (!dirFlag.empty())
    {
      migDir = fs::path(dirFlag);
      if (migDir.is_relative())
        migDir = fs::weakly_canonical(projectDir / migDir);
    }
    else
    {
      const std::string envDir = env_or("VIX_ORM_DIR", "");
      if (!envDir.empty())
      {
        fs::path p = fs::path(envDir);
        migDir = p.is_absolute() ? p : (projectDir / p);
      }
      else
      {
        migDir = detect_migrations_dir(projectDir);
      }

      if (migDir.empty())
        migDir = projectDir / "migrations";

      migDir = fs::weakly_canonical(migDir);
    }

    // Ensure migrations dir:
    // - migrate/rollback/status => must exist
    // - makemigrations          => create it if missing
    if (!fs::exists(migDir) || !fs::is_directory(migDir))
    {
      if (!is_make)
      {
        std::cerr << "vix orm: migrations directory not found: " << migDir.string() << "\n";
        if (looks_like_app_root(projectDir))
        {
          std::cerr << "Tip: create it with:\n";
          std::cerr << "  mkdir -p " << (projectDir / "migrations").string() << "\n";
        }
        std::cerr << "Or pass: --dir <path>\n";
        return 1;
      }
      else
      {
        std::error_code ec;
        fs::create_directories(migDir, ec);
        if (ec)
        {
          std::cerr << "vix orm makemigrations: cannot create migrations dir: " << migDir.string() << "\n";
          return 1;
        }
      }
    }

    // Tool path
    std::string tool = get_flag(args, "--tool");
    if (tool.empty())
      tool = find_migrator_tool();

    std::string cmd;
    cmd += shell_quote(tool) + " ";

    if (is_make)
    {
      // REQUIRED
      const std::string newSchema = parse_opt("--new");
      if (newSchema.empty())
      {
        std::cerr << "vix orm makemigrations requires: --new <schema.json>\n";
        return 1;
      }

      std::string snapshot = parse_opt("--snapshot");
      if (snapshot.empty())
        snapshot = "schema.json";

      std::string name = parse_opt("--name");
      if (name.empty())
        name = "auto";

      std::string dialect = parse_opt("--dialect");
      if (dialect.empty())
        dialect = "mysql";

      fs::path newPath = fs::path(newSchema);
      if (newPath.is_relative())
        newPath = fs::weakly_canonical(projectDir / newPath);

      fs::path snapPath = fs::path(snapshot);
      if (snapPath.is_relative())
        snapPath = fs::weakly_canonical(projectDir / snapPath);

      cmd += "makemigrations ";
      cmd += "--new " + shell_quote(newPath.string()) + " ";
      cmd += "--snapshot " + shell_quote(snapPath.string()) + " ";
      cmd += "--dir " + shell_quote(migDir.string()) + " ";
      cmd += "--name " + shell_quote(name) + " ";
      cmd += "--dialect " + shell_quote(dialect);
    }
    else
    {
      const std::string host = flag_or_env("--host", "VIX_ORM_HOST", "tcp://127.0.0.1:3306");
      const std::string user = flag_or_env("--user", "VIX_ORM_USER", "root");
      const std::string pass = flag_or_env("--pass", "VIX_ORM_PASS", "");
      const std::string db = flag_or_env("--db", "VIX_ORM_DB", "vixdb");

      cmd += shell_quote(host) + " ";
      cmd += shell_quote(user) + " ";
      cmd += shell_quote(pass) + " ";
      cmd += shell_quote(db) + " ";
      cmd += shell_quote(sub);

      if (sub == "rollback")
      {
        std::string steps = get_flag(args, "--steps");
        if (steps.empty())
        {
          std::cerr << "vix orm rollback requires: --steps <n>\n";
          return 1;
        }
        cmd += " --steps " + shell_quote(steps);
      }

      cmd += " --dir " + shell_quote(migDir.string());
    }

    int rc = std::system(cmd.c_str());
    return rc == 0 ? 0 : 1;
  }

  int help()
  {
    std::ostream &out = std::cout;
    out << "Vix ORM\n";
    out << "Database migrations & schema management\n\n";

    out << "Usage:\n";
    out << "  vix orm migrate   [options]\n";
    out << "  vix orm rollback  --steps <n> [options]\n";
    out << "  vix orm status    [options]\n\n";
    out << "  vix orm makemigrations --new <schema.json> [options]\n\n";

    out << "Makemigrations options:\n";
    out << "  --new <path>          New schema json (required)\n";
    out << "  --snapshot <path>     Previous snapshot (default: schema.json)\n";
    out << "  --name <label>        Migration label (default: auto)\n";
    out << "  --dialect <mysql|sqlite>  SQL dialect (default: mysql)\n\n";

    out << "Common options:\n";
    out << "  --db <name>           Database name (overrides VIX_ORM_DB)\n";
    out << "  --dir <path>          Migrations directory (overrides VIX_ORM_DIR)\n";
    out << "  --host <uri>          MySQL URI (default: tcp://127.0.0.1:3306)\n";
    out << "  --user <name>         Database user (default: root)\n";
    out << "  --pass <pass>         Database password\n";
    out << "  --project-dir <path>  Force project root detection\n";
    out << "  --tool <path>         Override migrator executable path\n";
    out << "  -h, --help            Show this help\n\n";

    out << "Rollback options:\n";
    out << "  --steps <n>           Rollback last N applied migrations (required)\n\n";

    out << "Environment defaults:\n";
    out << "  VIX_ORM_HOST   Default DB host (tcp://127.0.0.1:3306)\n";
    out << "  VIX_ORM_USER   Default DB user (root)\n";
    out << "  VIX_ORM_PASS   Default DB password\n";
    out << "  VIX_ORM_DB     Default DB name (vixdb)\n";
    out << "  VIX_ORM_DIR    Default migrations dir (migrations)\n";
    out << "  VIX_ORM_TOOL   Default migrator executable path\n\n";

    out << "Examples:\n";
    out << "  vix orm migrate --db blog_db --dir ./migrations\n";
    out << "  vix orm rollback --steps 1 --db blog_db --dir ./migrations\n";
    out << "  vix orm status --db blog_db\n";
    out << "  VIX_ORM_DB=blog_db vix orm migrate --dir ./migrations\n";
    out << "  vix orm makemigrations --new ./schema.new.json --snapshot ./schema.json --dir ./migrations --name create_users --dialect mysql\n";

    return 0;
  }

}
