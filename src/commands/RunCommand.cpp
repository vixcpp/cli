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
#include <fstream>

#ifndef _WIN32
#include <unistd.h>     // pipe, dup2, read, write, close
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // waitpide, WIFEXITED, WEXITSTATUS
#include <sys/select.h> // select, fd_set, FD_SET, FD_ZERO, FD_ISSET
#endif

namespace fs = std::filesystem;
using namespace vix::cli::style;

namespace
{
#ifndef _WIN32
    struct SigintGuard
    {
        struct sigaction oldAction{};
        bool installed = false;

        SigintGuard()
        {
            struct sigaction sa{};
            sa.sa_handler = SIG_IGN; // ignorer SIGINT dans le parent
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;

            if (sigaction(SIGINT, &sa, &oldAction) == 0)
                installed = true;
        }

        ~SigintGuard()
        {
            if (installed)
            {
                sigaction(SIGINT, &oldAction, nullptr); // restaurer le handler d’avant
            }
        }
    };
#endif
    inline void write_safe(int fd, const char *buf, ssize_t n)
    {
        if (n > 0)
        {
            const ssize_t written = ::write(fd, buf, static_cast<size_t>(n));
            (void)written;
        }
    }

    int run_cmd_live_filtered(const std::string &cmd)
    {
#ifdef _WIN32
        // Windows: on garde std::system, pas de filtrage avancé.
        return std::system(cmd.c_str());
#else
        SigintGuard sigGuard; // le parent ignore SIGINT pendant le build/run

        int outPipe[2];
        int errPipe[2];

        if (pipe(outPipe) != 0 || pipe(errPipe) != 0)
        {
            return std::system(cmd.c_str());
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            close(outPipe[0]);
            close(outPipe[1]);
            close(errPipe[0]);
            close(errPipe[1]);
            return std::system(cmd.c_str());
        }

        if (pid == 0)
        {
            // ----- Child -----
            struct sigaction saChild{};
            saChild.sa_handler = SIG_DFL;
            sigemptyset(&saChild.sa_mask);
            saChild.sa_flags = 0;
            sigaction(SIGINT, &saChild, nullptr);

            close(outPipe[0]);
            close(errPipe[0]);

            dup2(outPipe[1], STDOUT_FILENO);
            dup2(errPipe[1], STDERR_FILENO);

            close(outPipe[1]);
            close(errPipe[1]);

            execl("/bin/sh", "sh", "-c", cmd.c_str(), (char *)nullptr);
            _exit(127);
        }

        // ----- Parent -----
        close(outPipe[1]);
        close(errPipe[1]);

        fd_set fds;
        bool running = true;
        int exitCode = 0;

        const std::string ninjaStop = "ninja: build stopped: interrupted by user.";
        // NEW: patterns pour make/gmake en cas d’interruption CTRL+C
        const std::string makeInterrupt1 = "make: ***";
        const std::string makeInterrupt2 = "gmake: ***";
        const std::string interruptWord = "Interrupt";

        auto is_interrupt_noise = [&](const std::string &chunk) -> bool
        {
            // On ne masque que le bruit quand l’utilisateur stoppe avec Ctrl+C
            if (chunk.find(interruptWord) == std::string::npos)
                return false;

            // Cas Ninja déjà géré ailleurs, on laisse comme ça
            if (chunk.find(ninjaStop) != std::string::npos)
                return true;

            // Cas Make / gmake "Interrupt"
            if (chunk.find(makeInterrupt1) != std::string::npos)
                return true;
            if (chunk.find(makeInterrupt2) != std::string::npos)
                return true;

            // Par sécurité on garde tout le reste
            return false;
        };

        while (running)
        {
            FD_ZERO(&fds);
            FD_SET(outPipe[0], &fds);
            FD_SET(errPipe[0], &fds);

            int maxfd = std::max(outPipe[0], errPipe[0]) + 1;
            int ready = select(maxfd, &fds, nullptr, nullptr, nullptr);
            if (ready <= 0)
                continue;

            // stdout → live, filtré
            if (FD_ISSET(outPipe[0], &fds))
            {
                char buf[4096];
                ssize_t n = read(outPipe[0], buf, sizeof(buf));
                if (n > 0)
                {
                    std::string chunk(buf, static_cast<std::size_t>(n));

                    // NEW: on jette les lignes de bruit Ninja/make/gmake interrompu
                    if (!is_interrupt_noise(chunk))
                    {
                        write_safe(STDOUT_FILENO, chunk.data(),
                                   static_cast<ssize_t>(chunk.size()));
                    }
                }
            }

            // stderr → live, filtré
            if (FD_ISSET(errPipe[0], &fds))
            {
                char buf[4096];
                ssize_t n = read(errPipe[0], buf, sizeof(buf));
                if (n > 0)
                {
                    std::string chunk(buf, static_cast<std::size_t>(n));

                    if (!is_interrupt_noise(chunk))
                    {
                        write_safe(STDERR_FILENO, chunk.data(),
                                   static_cast<ssize_t>(chunk.size()));
                    }
                }
            }

            // vérifier si le process enfant est terminé
            int status = 0;
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid)
            {
                running = false;

                if (WIFEXITED(status))
                {
                    exitCode = WEXITSTATUS(status);
                }
                else if (WIFSIGNALED(status))
                {
                    int sig = WTERMSIG(status);
                    if (sig == SIGINT)
                        exitCode = 130;
                    else
                        exitCode = 128 + sig;
                }
                else
                {
                    exitCode = 1;
                }
            }
        }

        close(outPipe[0]);
        close(errPipe[0]);

        return exitCode;
#endif
    }

}

namespace vix::commands::RunCommand
{
    namespace
    {
        struct Options
        {
            std::string appName;
            std::string preset = "dev-ninja";
            std::string runPreset;
            std::string dir;
            int jobs = 0;

            bool quiet = false;
            bool verbose = false;
            std::string logLevel;

            std::string exampleName;

            // NEW
            bool singleCpp = false;
            std::filesystem::path cppFile;
        };

        std::filesystem::path get_scripts_root()
        {
            auto cwd = std::filesystem::current_path();
            return cwd / ".vix-scripts";
        }

        std::string make_script_cmakelists(const std::string &exeName,
                                           const std::filesystem::path &cppPath)
        {
            std::string s;
            s += "cmake_minimum_required(VERSION 3.20)\n";
            s += "project(" + exeName + " LANGUAGES CXX)\n\n";
            s += "set(CMAKE_CXX_STANDARD 20)\n";
            s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

            s += "list(APPEND CMAKE_PREFIX_PATH \n";
            s += "  \"/usr/local\"\n";
            s += "  \"/usr/local/lib/cmake\"\n";
            s += "  \"/usr/local/lib/cmake/vix\"\n";
            s += "  \"/usr/local/lib/cmake/Vix\"\n";
            s += ")\n\n";

            // ===== fmt shim (comme dans les apps normales) =====
            s += "find_package(fmt QUIET)\n";
            s += "if (TARGET fmt::fmt-header-only AND NOT TARGET fmt::fmt)\n";
            s += "  add_library(fmt::fmt ALIAS fmt::fmt-header-only)\n";
            s += "endif()\n";
            s += "if (NOT TARGET fmt::fmt)\n";
            s += "  add_library(fmt::fmt INTERFACE IMPORTED)\n";
            s += "  target_include_directories(fmt::fmt INTERFACE \"/usr/include\" \"/usr/local/include\")\n";
            s += "endif()\n\n";

            // ===== Boost + filesystem shim =====
            s += "find_package(Boost REQUIRED COMPONENTS system thread filesystem)\n";
            s += "if (NOT TARGET Boost::filesystem)\n";
            s += "  add_library(Boost::filesystem UNKNOWN IMPORTED)\n";
            s += "  set_target_properties(Boost::filesystem PROPERTIES\n";
            s += "    IMPORTED_LOCATION \"${Boost_FILESYSTEM_LIBRARY}\"\n";
            s += "    INTERFACE_INCLUDE_DIRECTORIES \"${Boost_INCLUDE_DIRS}\")\n";
            s += "endif()\n\n";

            // ===== OpenSSL shim (pour satisfaire l'interface de vix::core) =====
            s += "find_package(OpenSSL QUIET)\n";
            s += "if (OpenSSL_FOUND)\n";
            s += "  if (NOT TARGET OpenSSL::SSL)\n";
            s += "    add_library(OpenSSL::SSL UNKNOWN IMPORTED)\n";
            s += "    set_target_properties(OpenSSL::SSL PROPERTIES\n";
            s += "      IMPORTED_LOCATION \"${OPENSSL_SSL_LIBRARY}\"\n";
            s += "      INTERFACE_INCLUDE_DIRECTORIES \"${OPENSSL_INCLUDE_DIR}\")\n";
            s += "  endif()\n";
            s += "  if (NOT TARGET OpenSSL::Crypto AND DEFINED OPENSSL_CRYPTO_LIBRARY)\n";
            s += "    add_library(OpenSSL::Crypto UNKNOWN IMPORTED)\n";
            s += "    set_target_properties(OpenSSL::Crypto PROPERTIES\n";
            s += "      IMPORTED_LOCATION \"${OPENSSL_CRYPTO_LIBRARY}\"\n";
            s += "      INTERFACE_INCLUDE_DIRECTORIES \"${OPENSSL_INCLUDE_DIR}\")\n";
            s += "  endif()\n";
            s += "else()\n";
            s += "  # Fallback minimal: targets INTERFACE pour satisfaire VixConfig\n";
            s += "  if (NOT TARGET OpenSSL::SSL)\n";
            s += "    add_library(OpenSSL::SSL INTERFACE IMPORTED)\n";
            s += "  endif()\n";
            s += "  if (NOT TARGET OpenSSL::Crypto)\n";
            s += "    add_library(OpenSSL::Crypto INTERFACE IMPORTED)\n";
            s += "  endif()\n";
            s += "endif()\n\n";

            // ===== Vix (core) =====
            s += "set(vix_FOUND FALSE)\n";
            s += "find_package(vix QUIET CONFIG)\n";
            s += "if (vix_FOUND)\n";
            s += "  message(STATUS \"Found vix (lowercase) package config\")\n";
            s += "else()\n";
            s += "  find_package(Vix QUIET CONFIG)\n";
            s += "  if (Vix_FOUND)\n";
            s += "    message(STATUS \"Found Vix (legacy) package config\")\n";
            s += "    set(vix_FOUND TRUE)\n";
            s += "  endif()\n";
            s += "endif()\n\n";

            s += "if (NOT vix_FOUND)\n";
            s += "  message(FATAL_ERROR \"Could not find Vix/vix package config\")\n";
            s += "endif()\n\n";

            s += "if (TARGET vix::vix)\n";
            s += "  set(VIX_MAIN_TARGET vix::vix)\n";
            s += "elseif (TARGET Vix::vix)\n";
            s += "  set(VIX_MAIN_TARGET Vix::vix)\n";
            s += "else()\n";
            s += "  message(FATAL_ERROR \"No Vix main target found\")\n";
            s += "endif()\n\n";

            // ===== Executable + liens =====
            s += "add_executable(" + exeName + " \"" + cppPath.string() + "\")\n\n";
            s += "target_link_libraries(" + exeName + " PRIVATE\n";
            s += "  ${VIX_MAIN_TARGET}\n";
            s += "  Boost::system Boost::thread Boost::filesystem\n";
            s += "  fmt::fmt\n";
            s += "  OpenSSL::SSL OpenSSL::Crypto\n";
            s += ")\n\n";

            // Target run
            s += "add_custom_target(run\n";
            s += "  COMMAND $<TARGET_FILE:" + exeName + ">\n";
            s += "  DEPENDS " + exeName + "\n";
            s += "  USES_TERMINAL\n";
            s += ")\n";

            return s;
        }

        // Forward declarations for helpers used by run_single_cpp
        std::string quote(const std::string &s);

        std::string choose_run_preset(const fs::path &dir,
                                      const std::string &configurePreset,
                                      const std::string &userRunPreset);

        void handle_runtime_exit_code(int code, const std::string &context);

        int run_single_cpp(const Options &opt)
        {
            using namespace std;
            using namespace std::filesystem;

            const path script = opt.cppFile;
            if (!exists(script))
            {
                error("C++ file not found: " + script.string());
                return 1;
            }

            const string exeName = script.stem().string();
            path scriptsRoot = get_scripts_root();
            create_directories(scriptsRoot);

            // Dossier de build dédié à ce script
            path projectDir = scriptsRoot / exeName;

            create_directories(projectDir);
            path cmakeLists = projectDir / "CMakeLists.txt";

            // Générer un CMakeLists minimal pour ce script
            {
                ofstream ofs(cmakeLists);
                ofs << make_script_cmakelists(exeName, script);
            }

            info("Script mode: compiling " + script.string());
            info("Using script build directory:");
            step(projectDir.string());
            std::cout << "\n";

            // 1) Configure sans presets : cmake -S . -B build
            {
                std::ostringstream oss;
                oss << "cd " << quote(projectDir.string())
                    << " && cmake -S . -B build";

                int code = run_cmd_live_filtered(oss.str());
                if (code != 0)
                {
                    error("Script configure failed.");
                    return code;
                }
            }

            // 2) Build + run target "run" (définie dans make_script_cmakelists)
            {
                std::ostringstream oss;
                oss << "cd " << quote(projectDir.string())
                    << " && cmake --build build --target run";

                if (opt.jobs > 0)
                    oss << " -- -j " << opt.jobs;

                int code = run_cmd_live_filtered(oss.str());
                if (code != 0)
                {
                    handle_runtime_exit_code(code, "Script execution failed");
                    return code;
                }
            }

            return 0;
        }

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

                        // Single-file mode: vix run main.cpp
                        std::filesystem::path p{a};
                        if (p.extension() == ".cpp")
                        {
                            o.singleCpp = true;
                            o.cppFile = std::filesystem::absolute(p);
                        }
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

        // ---------------------------------------------------------------------
        // Helper: interpréter le code de retour du process runtime
        // ---------------------------------------------------------------------
        void handle_runtime_exit_code(int code, const std::string &context)
        {
            if (code == 0)
                return;

            if (code == 2 || code == 130)
            {
                hint("ℹ Server interrupted by user (SIGINT).");
                return;
            }

            error(context + " (exit code " + std::to_string(code) + ").");
            hint("Check the logs above or run the command manually.");
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

        // Est-ce que le log de build contient du "vrai travail" ?
        static bool has_real_build_work(const std::string &log)
        {
            // S’il y a des lignes "Building", "Linking", "Compiling", etc. → vrai travail
            if (log.find("Building") != std::string::npos)
                return true;
            if (log.find("Linking") != std::string::npos)
                return true;
            if (log.find("Compiling") != std::string::npos)
                return true;
            if (log.find("Scanning dependencies") != std::string::npos)
                return true;

            // Cas Ninja : "ninja: no work to do." → clairement no-op
            if (log.find("no work to do") != std::string::npos)
                return false;

            // Si on ne voit que des "Built target ..." ça sent le Make qui spamme sans rien faire
            bool hasBuiltTarget = log.find("Built target") != std::string::npos;
            if (hasBuiltTarget)
            {
                // Pas de "Building"/"Linking" + seulement "Built target" → no-op
                return false;
            }

            // Par défaut, on considère qu’il y a eu du travail (pour ne pas masquer des choses importantes)
            return true;
        }

        // Exécuter une commande et capturer stdout (et le code de retour)
        static std::string run_and_capture_with_code(const std::string &cmd, int &exitCode)
        {
            std::string out;
#if defined(_WIN32)
            FILE *p = _popen(cmd.c_str(), "r");
#else
            FILE *p = popen(cmd.c_str(), "r");
#endif
            if (!p)
            {
                exitCode = -1;
                return out;
            }

            char buf[4096];
            while (fgets(buf, sizeof(buf), p))
                out.append(buf);

#if defined(_WIN32)
            exitCode = _pclose(p);
#else
            exitCode = pclose(p);
#endif
            return out;
        }

        // Version simple quand on n'a pas besoin du code (pour list_presets)
        static std::string run_and_capture(const std::string &cmd)
        {
            int code = 0;
            return run_and_capture_with_code(cmd, code);
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

        std::string choose_run_preset(const fs::path &dir,
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

        if (opt.singleCpp)
        {
            return run_single_cpp(opt);
        }

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

                int code = 0;
                std::string configureLog = run_and_capture_with_code(cmd, code);

                if (code != 0)
                {
                    // ❌ En cas d'erreur → on montre tout le log CMake
                    if (!configureLog.empty())
                        std::cout << configureLog;

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

                // ✅ Succès :
                //  - mode normal  → on ne montre rien (UX clean)
                //  - mode verbose → on affiche le log complet CMake
                if (opt.verbose && !configureLog.empty())
                    std::cout << configureLog;

                success("Configure step completed.");
                std::cout << "\n";
            }

            // 2) Choisir run preset (ou build preset avec target run)
            const std::string runPreset =
                choose_run_preset(projectDir, opt.preset, opt.runPreset);

            // 3) Build + run (target run) — **on revient à std::system pour garder les logs live**
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
                // hint("Command: " + cmd);

                const int code = run_cmd_live_filtered(cmd);
                if (code != 0)
                {
                    handle_runtime_exit_code(
                        code,
                        "Execution failed (run preset '" + runPreset + "')");
                    return code;
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
            const int code = run_cmd_live_filtered(cmd);
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
            oss << "cd /D " << quote(buildDir.string()) << " && cmake --build .";
            if (opt.jobs > 0)
                oss << " -j " << opt.jobs;
#else
            oss << "cd " << quote(buildDir.string()) << " && cmake --build .";
            if (opt.jobs > 0)
                oss << " -j " << opt.jobs;
#endif
            const std::string cmd = oss.str();

            info("Building project (fallback mode)...");

            int code = 0;
            std::string buildLog = run_and_capture_with_code(cmd, code);

            if (code != 0)
            {
                // En cas d’erreur → on affiche TOUT le log pour debug
                std::cout << buildLog;
                error("Build failed (fallback build/, code " + std::to_string(code) + ").");
                hint("Check the build logs or run the command manually.");
                return code != 0 ? code : 5;
            }

            // Succès : décider si on affiche le log ou non
            if (has_real_build_work(buildLog))
            {
                // Il y a eu de la compilation / linking → on montre tout
                std::cout << buildLog;
                success("Build completed (fallback).");
            }
            else
            {
                // Aucun vrai travail → on garde l’UX propre
                success("Nothing to build — everything is up to date.");
            }

            std::cout << "\n";
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
                    handle_runtime_exit_code(
                        code,
                        "Example returned non-zero exit code");
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
                handle_runtime_exit_code(
                    code,
                    "Executable returned non-zero exit code");
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
