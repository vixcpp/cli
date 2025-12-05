#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/Style.hpp>

#include <filesystem>
#include <cstdlib>
#include <string>
#include <vector>
#include <optional>
#include <sstream>
#include <cstdio>
#include <algorithm>

namespace fs = std::filesystem;
using namespace vix::cli::style;

namespace vix::commands::RunCommand
{
    namespace
    {
        struct Options
        {
            std::string appName;
            std::string preset = "dev-ninja"; // configure preset
            std::string runPreset;            // run preset (facultatif)
            std::string dir;
            int jobs = 0;

            // Contrôle du logging runtime
            bool quiet = false;   // vix run --quiet
            bool verbose = false; // vix run --verbose
            std::string logLevel; // vix run --log-level <level>

            // Mode "example" pour l’umbrella repo (vix run example main)
            std::string exampleName; // si appName == "example", c’est ici qu’on stocke le nom
        };

        // ---------------------------------------------------------------------
        // Helpers: parsing des options
        // ---------------------------------------------------------------------

        std::optional<std::string> pick_dir_opt_local(const std::vector<std::string> &args)
        {
            auto is_opt = [](std::string_view s)
            { return !s.empty() && s.front() == '-'; };

            for (size_t i = 0; i < args.size(); ++i)
            {
                const auto &a = args[i];

                if (a == "-d" || a == "--dir")
                {
                    if (i + 1 < args.size() && !is_opt(args[i + 1]))
                        return args[i + 1];
                    return std::nullopt;
                }

                constexpr const char pfx[] = "--dir=";
                if (a.rfind(pfx, 0) == 0)
                {
                    std::string v = a.substr(sizeof(pfx) - 1);
                    if (v.empty())
                        return std::nullopt;
                    return v;
                }
            }
            return std::nullopt;
        }

        Options parse(const std::vector<std::string> &args)
        {
            Options o;

            for (size_t i = 0; i < args.size(); ++i)
            {
                const auto &a = args[i];

                // --- Options spécifiques à run ---
                if (a == "--preset" && i + 1 < args.size())
                {
                    o.preset = args[++i];
                }
                else if (a == "--run-preset" && i + 1 < args.size())
                {
                    o.runPreset = args[++i];
                }
                else if ((a == "-j" || a == "--jobs") && i + 1 < args.size())
                {
                    try
                    {
                        o.jobs = std::stoi(args[++i]);
                    }
                    catch (...)
                    {
                        o.jobs = 0;
                    }
                }
                // --- Contrôle du logging runtime ---
                else if (a == "--quiet" || a == "-q")
                {
                    o.quiet = true;
                }
                else if (a == "--verbose")
                {
                    o.verbose = true;
                }
                else if ((a == "--log-level" || a == "--loglevel") && i + 1 < args.size())
                {
                    o.logLevel = args[++i];
                }
                else if (a.rfind("--log-level=", 0) == 0)
                {
                    o.logLevel = a.substr(std::string("--log-level=").size());
                }
                // --- Arguments positionnels (appName, exampleName, etc.) ---
                else if (!a.empty() && a != "--" && a[0] != '-')
                {
                    // Premier argument non-option → appName
                    if (o.appName.empty())
                    {
                        o.appName = a;
                    }
                    // Cas spécial : vix run example <name>
                    else if (o.appName == "example" && o.exampleName.empty())
                    {
                        o.exampleName = a;
                    }
                    // Autres arguments non-options : pour l’instant on ignore
                }
            }

            if (auto d = pick_dir_opt_local(args))
                o.dir = *d;

            return o;
        }

        std::string quote(const std::string &s)
        {
#ifdef _WIN32
            return "\"" + s + "\"";
#else
            if (s.find_first_of(" \t\"'\\$`") != std::string::npos)
                return "'" + s + "'";
            return s;
#endif
        }

#ifndef _WIN32
        static std::string run_and_capture(const std::string &cmd)
        {
            std::string out;
            FILE *p = popen(cmd.c_str(), "r");
            if (!p)
                return out;
            char buf[4096];
            while (fgets(buf, sizeof(buf), p))
                out.append(buf);
            pclose(p);
            return out;
        }
#endif

        static bool has_presets(const fs::path &projectDir)
        {
            std::error_code ec;
            return fs::exists(projectDir / "CMakePresets.json", ec) ||
                   fs::exists(projectDir / "CMakeUserPresets.json", ec);
        }

        static std::vector<std::string> list_presets(const fs::path &dir, const std::string &kind)
        {
#ifdef _WIN32
            (void)dir;
            (void)kind;
            return {};
#else
            std::ostringstream oss;
            oss << "cd " << quote(dir.string()) << " && cmake --list-presets=" << kind;
            auto out = run_and_capture(oss.str());
            std::vector<std::string> names;
            std::istringstream is(out);
            std::string line;
            while (std::getline(is, line))
            {
                auto q1 = line.find('\"');
                auto q2 = line.find('\"', q1 == std::string::npos ? q1 : q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1)
                    names.emplace_back(line.substr(q1 + 1, q2 - (q1 + 1)));
            }
            return names;
#endif
        }

        static std::string choose_run_preset(const fs::path &dir,
                                             const std::string &configurePreset,
                                             const std::string &userRunPreset)
        {
            // Les presets "run-*" sont aussi des buildPresets avec target=["run"]
            auto runs = list_presets(dir, "build");
            auto has = [&](const std::string &n)
            {
                return std::find(runs.begin(), runs.end(), n) != runs.end();
            };

            if (!userRunPreset.empty() && (runs.empty() || has(userRunPreset)))
                return userRunPreset;

            if (!runs.empty())
            {
                // run-dev-ninja
                if (has("run-" + configurePreset))
                    return "run-" + configurePreset;

                // dev-ninja → run-ninja
                if (configurePreset.rfind("dev-", 0) == 0)
                {
                    std::string mapped = "run-" + configurePreset.substr(4);
                    if (has(mapped))
                        return mapped;
                }

                // sinon on tentera "run-ninja"
                if (has("run-ninja"))
                    return "run-ninja";

                // fallback: build-ninja, ou le premier
                if (has("build-ninja"))
                    return "build-ninja";

                return runs.front();
            }

            // Heuristique si pas de liste dispo
            if (configurePreset.rfind("dev-", 0) == 0)
                return std::string("run-") + configurePreset.substr(4);
            return "run-ninja";
        }

        // Vérifie si le build/ existe déjà et a un CMakeCache.txt
        static bool has_cmake_cache(const fs::path &buildDir)
        {
            std::error_code ec;
            return fs::exists(buildDir / "CMakeCache.txt", ec);
        }

        std::optional<fs::path> choose_project_dir(const Options &opt, const fs::path &cwd)
        {
            auto exists_cml = [](const fs::path &p)
            {
                std::error_code ec;
                return fs::exists(p / "CMakeLists.txt", ec);
            };

            if (!opt.dir.empty() && exists_cml(opt.dir))
                return fs::path(opt.dir);

            if (exists_cml(cwd))
                return cwd;

            if (!opt.appName.empty())
            {
                fs::path a = opt.appName;
                if (exists_cml(a))
                    return a;
                if (exists_cml(cwd / a))
                    return cwd / a;
            }

            // dernier recours : cwd, mais on considère que c’est un projet
            return cwd;
        }

        // ---------------------------------------------------------------------
        // Pont CLI -> serveur: injecter VIX_LOG_LEVEL dans l'environnement
        // ---------------------------------------------------------------------
        void apply_log_level_env(const Options &opt)
        {
            // Priorité:
            //  1) --log-level <level>
            //  2) --quiet  => warn
            //  3) --verbose => debug
            //  4) sinon: ne rien toucher (on laisse VIX_LOG_LEVEL existant, ou défaut App)
            std::string level;

            if (!opt.logLevel.empty())
            {
                level = opt.logLevel;
            }
            else if (opt.quiet)
            {
                level = "warn";
            }
            else if (opt.verbose)
            {
                level = "debug";
            }

            if (level.empty())
                return;

#if defined(_WIN32)
            _putenv_s("VIX_LOG_LEVEL", level.c_str());
#else
            ::setenv("VIX_LOG_LEVEL", level.c_str(), 1);
#endif
        }

    } // namespace

    // -------------------------------------------------------------------------
    // Entrée principale: vix run [...]
    // -------------------------------------------------------------------------
    int run(const std::vector<std::string> &args)
    {
        const Options opt = parse(args);
        const fs::path cwd = fs::current_path();

        auto projectDirOpt = choose_project_dir(opt, cwd);
        if (!projectDirOpt)
        {
            error("Unable to determine the project folder.");
            hint("Try: vix run --dir <path> or run the command from a Vix project directory.");
            return 1;
        }

        const fs::path projectDir = *projectDirOpt;

        // Appliquer le niveau de log pour le runtime (serveur)
        apply_log_level_env(opt);

        info("Using project directory:");
        step(projectDir.string());
        std::cout << "\n";

        // ---------------------------------------------------------------------
        // CAS 1 : Projet avec CMakePresets.json → flow moderne (configure + run preset)
        // ---------------------------------------------------------------------
        if (has_presets(projectDir))
        {
            // 1) Configure avec preset
            {
                std::ostringstream oss;
#ifdef _WIN32
                oss << "cmd /C \"cd /D " << quote(projectDir.string())
                    << " && cmake --preset " << quote(opt.preset) << "\"";
#else
                oss << "cd " << quote(projectDir.string())
                    << " && cmake --preset " << quote(opt.preset);
#endif
                const std::string cmd = oss.str();

                info("Configuring project (preset: " + opt.preset + ")...");
                // hint("Command: " + cmd); // à activer un jour pour un vrai --verbose CLI

                const int code = std::system(cmd.c_str());
                if (code != 0)
                {
                    error("CMake configure failed with preset '" + opt.preset + "'.");
                    hint("Run the same command manually to inspect the error:");
#ifdef _WIN32
                    step("cmake --preset " + opt.preset);
#else
                    step("cd " + projectDir.string());
                    step("cmake --preset " + opt.preset);
#endif
                    return code != 0 ? code : 2;
                }

                success("Configure step completed.");
                std::cout << "\n";
            }

            // 2) Choisir run preset (ou build preset avec target run)
            const std::string runPreset = choose_run_preset(projectDir, opt.preset, opt.runPreset);

            // 3) Build + run (target run)
            {
                std::ostringstream oss;
#ifdef _WIN32
                oss << "cmd /C \"cd /D " << quote(projectDir.string())
                    << " && cmake --build --preset " << quote(runPreset) << " --target run";
                if (opt.jobs > 0)
                    oss << " -- -j " << opt.jobs;
                oss << "\"";
#else
                oss << "cd " << quote(projectDir.string())
                    << " && cmake --build --preset " << quote(runPreset) << " --target run";
                if (opt.jobs > 0)
                    oss << " -- -j " << opt.jobs; // backend args
#endif
                const std::string cmd = oss.str();

                info("Building & running (preset: " + runPreset + ")...");
                // hint("Command: " + cmd); // idem: pour un futur --verbose

                const int code = std::system(cmd.c_str());
                if (code != 0)
                {
                    error("Execution failed (run preset '" + runPreset + "', code " + std::to_string(code) + ").");
                    hint("Check the build errors above or run the command manually.");
                    return code != 0 ? code : 3;
                }
            }

            success("🏃 Application started (preset: " + runPreset + ").");
            return 0;
        }

        // ---------------------------------------------------------------------
        // CAS 2 : Pas de presets → fallback classique build/ + cmake ..
        // ---------------------------------------------------------------------
        fs::path buildDir = projectDir / "build";

        // Créer le dossier build/ si besoin
        {
            std::error_code ec;
            fs::create_directories(buildDir, ec);
            if (ec)
            {
                error("Unable to create build directory: " + ec.message());
                return 1;
            }
        }

        // 1) Configure (cmake ..) uniquement si pas encore configuré
        if (!has_cmake_cache(buildDir))
        {
            std::ostringstream oss;
#ifdef _WIN32
            oss << "cmd /C \"cd /D " << quote(buildDir.string()) << " && cmake ..\"";
#else
            oss << "cd " << quote(buildDir.string()) << " && cmake ..";
#endif
            const std::string cmd = oss.str();

            info("Configuring project (fallback mode)...");
            const int code = std::system(cmd.c_str());
            if (code != 0)
            {
                error("CMake configure failed (fallback build/, code " + std::to_string(code) + ").");
                hint("Check your CMakeLists.txt or run the command manually.");
                return code != 0 ? code : 4;
            }

            success("Configure step completed (fallback).");
            std::cout << "\n";
        }
        else
        {
            info("CMake cache detected in build/ — skipping configure step (fallback).");
        }

        // 2) Build
        {
            std::ostringstream oss;
#ifdef _WIN32
            oss << "cmd /C \"cd /D " << quote(buildDir.string())
                << " && cmake --build .";
            if (opt.jobs > 0)
                oss << " -j " << opt.jobs;
            oss << "\"";
#else
            oss << "cd " << quote(buildDir.string()) << " && cmake --build .";
            if (opt.jobs > 0)
                oss << " -j " << opt.jobs;
#endif
            const std::string cmd = oss.str();

            info("Building project (fallback mode)...");
            const int code = std::system(cmd.c_str());
            if (code != 0)
            {
                error("Build failed (fallback build/, code " + std::to_string(code) + ").");
                hint("Check the build logs or run the command manually.");
                return code != 0 ? code : 5;
            }
        }

        // 3) Tentative de run d’un binaire portant le nom du projet (optionnel)
        const std::string exeName = projectDir.filename().string();
        fs::path exePath = buildDir / exeName;

#ifdef _WIN32
        exePath += ".exe";
#endif

        // Cas spécial : umbrella repo Vix → ne pas relancer le CLI lui-même.
        if (exeName == "vix")
        {
            success("Build completed (fallback).");

            // Mode dédié : vix run example <name>
            if (opt.appName == "example")
            {
                if (opt.exampleName.empty())
                {
                    error("No example name provided.");
                    hint("Usage: vix run example <name>");
                    hint("For instance: vix run example main");
                    return 1;
                }

                fs::path exampleExe = buildDir / opt.exampleName;
#ifdef _WIN32
                exampleExe += ".exe";
#endif

                if (!fs::exists(exampleExe))
                {
                    error("Example binary not found: " + exampleExe.string());
                    hint("Make sure the example exists and is enabled in CMake.");
                    hint("Existing examples are built in the build/ directory (e.g. main, now_server, hello_routes...).");
                    return 1;
                }

                info("Running example: " + opt.exampleName);
                std::string cmd =
#ifdef _WIN32
                    "\"" + exampleExe.string() + "\"";
#else
                    quote(exampleExe.string());
#endif
                const int code = std::system(cmd.c_str());
                if (code != 0)
                {
                    error("Example returned non-zero exit code: " + std::to_string(code));
                }
                return code;
            }

            // Pas de mode "example" → on documente simplement quoi faire
            hint("Detected the Vix umbrella repository.");
            hint("The CLI binary 'vix' and umbrella examples were built in the build/ directory.");
            hint("To run an example from here, use:");
            step("  vix run example main");
            step("  vix run example now_server");
            step("  vix run example hello_routes");
            return 0;
        }

        // Cas général : projet normal → on tente d’exécuter un binaire portant le nom du projet
        if (fs::exists(exePath))
        {
            info("Running executable: " + exePath.string());
            std::string cmd =
#ifdef _WIN32
                "\"" + exePath.string() + "\"";
#else
                quote(exePath.string());
#endif
            const int code = std::system(cmd.c_str());
            if (code != 0)
            {
                error("Executable returned non-zero exit code: " + std::to_string(code));
                return code;
            }
        }
        else
        {
            success("Build completed (fallback). No explicit 'run' target found.");
            hint("If you want to run a specific example or binary, execute it manually from the build/ directory.");
        }

        return 0;
    }

    int help()
    {
        std::ostream &out = std::cout;

        out << "Usage:\n";
        out << "  vix run [name] [options] [-- app-args...]\n";
        out << "\n";

        out << "Description:\n";
        out << "  Configure, build and run a Vix.cpp application using CMake presets.\n";
        out << "  The command ensures CMake is configured, then builds the 'run' target\n";
        out << "  with the selected preset, and finally executes the resulting binary.\n";
        out << "\n";

        out << "Options:\n";
        out << "  -d, --dir <path>        Explicit project directory\n";
        out << "  --preset <name>         Configure preset (CMakePresets.json), default: dev-ninja\n";
        out << "  --run-preset <name>     Build preset used to build target 'run'\n";
        out << "  -j, --jobs <n>          Number of parallel build jobs\n";
        out << "\n";

        out << "Global flags (from `vix`):\n";
        out << "  --verbose                Show debug logs from the runtime (log-level=debug)\n";
        out << "  -q, --quiet              Only show warnings and errors (log-level=warn)\n";
        out << "  --log-level <level>      Set runtime log-level for the app process\n";
        out << "\n";

        out << "Examples:\n";
        out << "  vix run\n";
        out << "  vix run api -- --port 8080\n";
        out << "  vix run --dir ./examples/blog\n";
        out << "  vix run api --preset dev-ninja --run-preset run-ninja\n";
        out << "  vix run example main              # in the umbrella repo, run ./build/main\n";
        out << "\n";
        out << "  vix --log-level debug run api          # run with debug logs from runtime\n";
        out << "  vix --quiet run api                    # minimal logs from runtime\n";

        return 0;
    }

}
