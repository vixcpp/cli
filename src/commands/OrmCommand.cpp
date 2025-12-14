#include <vix/cli/commands/OrmCommand.hpp>
#include <iostream>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace vix::commands
{
    static const char *env_or(const char *k, const char *defv)
    {
        if (const char *v = std::getenv(k))
            return v;
        return defv;
    }

    static std::string shell_quote(const std::string &s)
    {
        // simple safe quoting
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
        // 0) override (debug/dev)
        if (const char *t = std::getenv("VIX_ORM_TOOL"))
            return std::string(t);

        // 1) installed layout relative to the vix executable
        //    ex: /usr/local/bin/vix
        //        /usr/local/libexec/vix/vix_orm_migrator
        if (fs::path binDir = get_exe_dir(); !binDir.empty())
        {
            fs::path prefix = binDir.parent_path(); // .../usr/local (or .../usr)

            std::vector<fs::path> installed = {
                // recommended (GNUInstallDirs -> CMAKE_INSTALL_LIBEXECDIR)
                prefix / "libexec" / "vix" / "vix_orm_migrator",
                prefix / "libexec" / "vix" / "vix_orm_migrate_init",

                // fallback variants
                prefix / "lib" / "vix" / "libexec" / "vix_orm_migrator",
                prefix / "lib" / "vix" / "vix_orm_migrator",

                // same dir (portable)
                binDir / "vix_orm_migrator",
            };

            for (const auto &p : installed)
                if (!p.empty() && fs::exists(p))
                    return p.string();
        }

        // 2) dev mode (monorepo)
        // NOTE: in your repo you have:
        //   ./build/orm_build/migrate_init
        //   ./modules/orm/build/migrate_init
        // once you switch to tools/migrator, update these paths accordingly.
        std::vector<fs::path> dev = {
            fs::path("build/orm_build/vix_orm_migrator"),
            fs::path("modules/orm/build/vix_orm_migrator"),

            fs::path("build/orm_build/migrate_init"),
            fs::path("modules/orm/build/migrate_init"),

            fs::path("../build/orm_build/vix_orm_migrator"),
            fs::path("../orm/build/vix_orm_migrator"),
            fs::path("../build/orm_build/migrate_init"),
            fs::path("../orm/build/migrate_init"),
        };

        for (const auto &p : dev)
            if (fs::exists(p))
                return p.string();

        // 3) PATH fallback (installed in PATH)
        // (If you decide to install vix_orm_migrator into bin instead of libexec.)
        return "vix_orm_migrator";
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
        // heuristiques app
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
                bestVix = p; // on continue à remonter pour garder la vraie racine

            if (looks_like_app_root(p))
                best = p; // app root (plus spécifique)

            // fallback: n'importe quel CMakeLists
            if (best.empty() && fs::exists(p / "CMakeLists.txt"))
                best = p;

            if (!p.has_parent_path())
                break;
            fs::path parent = p.parent_path();
            if (parent == p)
                break;
            p = parent;
        }

        // priorité:
        // 1) racine Vix si trouvée
        // 2) app root si trouvée
        // 3) fallback cmake
        if (!bestVix.empty())
            return bestVix;
        if (!best.empty())
            return best;
        return {};
    }

    static fs::path detect_migrations_dir(const fs::path &projectDir)
    {
        // Si on est dans le repo Vix → on ne devine PAS
        if (looks_like_vix_repo_root(projectDir))
            return {};

        // Cas normal : projet applicatif
        std::vector<fs::path> candidates = {
            projectDir / "migrations",
            projectDir / "db" / "migrations",
            projectDir / "database" / "migrations",

            // extras utiles (souvent vus)
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

    int OrmCommand::help()
    {
        std::cout
            << "Vix ORM\n"
            << "Database migrations & schema management\n\n"

            << "Usage:\n"
            << "  vix orm migrate   [options]\n"
            << "  vix orm rollback  --steps <n> [options]\n"
            << "  vix orm status    [options]\n\n"

            << "Common options:\n"
            << "  --db <name>           Database name (overrides VIX_ORM_DB)\n"
            << "  --dir <path>          Migrations directory (overrides VIX_ORM_DIR)\n"
            << "  --host <uri>          MySQL URI (default: tcp://127.0.0.1:3306)\n"
            << "  --user <name>         Database user (default: root)\n"
            << "  --pass <pass>         Database password\n"
            << "  --project-dir <path>  Force project root detection\n"
            << "  --tool <path>         Override migrator executable path\n"
            << "  -h, --help            Show this help\n\n"

            << "Rollback options:\n"
            << "  --steps <n>           Rollback last N applied migrations (required)\n\n"

            << "Environment defaults:\n"
            << "  VIX_ORM_HOST   Default DB host (tcp://127.0.0.1:3306)\n"
            << "  VIX_ORM_USER   Default DB user (root)\n"
            << "  VIX_ORM_PASS   Default DB password\n"
            << "  VIX_ORM_DB     Default DB name (vixdb)\n"
            << "  VIX_ORM_DIR    Default migrations dir (migrations)\n"
            << "  VIX_ORM_TOOL   Default migrator executable path\n\n"

            << "Examples:\n"
            << "  vix orm migrate --db blog_db --dir ./migrations\n"
            << "  vix orm rollback --steps 1 --db blog_db --dir ./migrations\n"
            << "  vix orm status --db blog_db\n"
            << "  VIX_ORM_DB=blog_db vix orm migrate --dir ./migrations\n";

        return 0;
    }

    int OrmCommand::run(const std::vector<std::string> &args)
    {
        if (args.empty() || args[0] == "-h" || args[0] == "--help")
            return help();

        const std::string sub = args[0];
        if (sub != "migrate" && sub != "rollback" && sub != "status")
        {
            std::cerr << "vix: unknown orm subcommand '" << sub << "'\n\n";
            help();
            return 1;
        }

        // Small helper to avoid calling get_flag(...) twice everywhere
        auto flag_or_env = [&](const std::string &flag,
                               const char *envKey,
                               const char *defv) -> std::string
        {
            const std::string v = get_flag(args, flag);
            if (!v.empty())
                return v;
            return env_or(envKey, defv);
        };

        // Connection settings: flags override env defaults
        const std::string host = flag_or_env("--host", "VIX_ORM_HOST", "tcp://127.0.0.1:3306");
        const std::string user = flag_or_env("--user", "VIX_ORM_USER", "root");
        const std::string pass = flag_or_env("--pass", "VIX_ORM_PASS", "");
        const std::string db = flag_or_env("--db", "VIX_ORM_DB", "vixdb");

        // project dir: --project-dir or auto from cwd
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

        // migrations dir:
        //   a) --dir <path>
        //   b) VIX_ORM_DIR (abs or relative resolved from projectDir)
        //   c) auto-detect
        //   d) fallback: projectDir/migrations
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

        if (!fs::exists(migDir) || !fs::is_directory(migDir))
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

        // migrator tool:
        //   --tool overrides env/tool discovery
        std::string tool = get_flag(args, "--tool");
        if (tool.empty())
            tool = find_migrator_tool();

        // Build command:
        //   tool <host> <user> <pass> <db> <sub> [--steps N] --dir <path>
        std::string cmd;
        cmd += shell_quote(tool) + " ";
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

        // Always pass dir explicitly (absolute/canonical)
        cmd += " --dir " + shell_quote(migDir.string());

        int rc = std::system(cmd.c_str());
        return rc == 0 ? 0 : 1;
    }

}
