#include "vix/cli/commands/run/RunDetail.hpp"
#include <vix/cli/Style.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#ifndef _WIN32
#include <unistd.h>    // fork, execl, _exit
#include <sys/types.h> // pid_t
#include <sys/wait.h>  // waitpid
#include <signal.h>    // SIGINT, kill
#endif

using namespace vix::cli::style;

namespace vix::commands::RunCommand::detail
{
    namespace fs = std::filesystem;

    namespace
    {
        inline void print_watch_restart_banner(const fs::path &script)
        {
#ifdef _WIN32
            // Clear screen sous Windows
            std::system("cls");
#else
            // Clear screen avec séquences ANSI sur POSIX
            std::cout << "\x1b[2J\x1b[H" << std::flush;
#endif
            info(std::string("Watcher Restarting! File change detected: \"") + script.string() + "\"");
        }
    }

    bool script_uses_vix(const fs::path &cppPath)
    {
        std::ifstream ifs(cppPath);
        if (!ifs)
            return false;

        std::string line;
        while (std::getline(ifs, line))
        {
            if (line.find("vix::") != std::string::npos ||
                line.find("Vix::") != std::string::npos)
            {
                return true;
            }
            if (line.find("#include") == std::string::npos)
                continue;
            if (line.find("vix") != std::string::npos ||
                line.find("Vix") != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    fs::path get_scripts_root()
    {
        auto cwd = fs::current_path();
        return cwd / ".vix-scripts";
    }

    std::string make_script_cmakelists(const std::string &exeName,
                                       const fs::path &cppPath,
                                       bool useVixRuntime)
    {
        std::string s;
        s += "cmake_minimum_required(VERSION 3.20)\n";
        s += "project(" + exeName + " LANGUAGES CXX)\n\n";

        s += "if (NOT CMAKE_BUILD_TYPE)\n";
        s += " set(CMAKE_BUILD_TYPE Release CACHE STRING \"Build type\" FORCE)\n";
        s += "endif()\n\n";

        s += "set(CMAKE_CXX_STANDARD 20)\n";
        s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

        s += "set(GLOBAL_CXX_FLAGS \"-Wall -Wextra -Wshadow\")\n";
        s += "set(CMAKE_CXX_FLAGS_RELEASE \"${CMAKE_CXX_FLAGS_RELEASE} ${GLOBAL_CXX_FLAGS} -O3 -DNDEBUG\")\n";
        s += "set(CMAKE_CXX_FLAGS_DEBUG \"${CMAKE_CXX_FLAGS_DEBUG} ${GLOBAL_CXX_FLAGS} -O0 -g\")\n\n";

        if (!useVixRuntime)
        {
            s += "add_executable(" + exeName + " \"" + cppPath.string() + "\")\n\n";
            s += "if (UNIX AND NOT APPLE)\n";
            s += " target_link_libraries(" + exeName + " PRIVATE pthread dl)\n";
            s += "endif()\n\n";
            return s;
        }

        s += "list(APPEND CMAKE_PREFIX_PATH \n";
        s += " \"/usr/local\"\n";
        s += " \"/usr/local/lib/cmake\"\n";
        s += " \"/usr/local/lib/cmake/vix\"\n";
        s += " \"/usr/local/lib/cmake/Vix\"\n";
        s += ")\n\n";

        s += "find_package(fmt QUIET)\n";
        s += "if (TARGET fmt::fmt-header-only AND NOT TARGET fmt::fmt)\n";
        s += " add_library(fmt::fmt ALIAS fmt::fmt-header-only)\n";
        s += "endif()\n";
        s += "if (NOT TARGET fmt::fmt)\n";
        s += " add_library(fmt::fmt INTERFACE IMPORTED)\n";
        s += " target_include_directories(fmt::fmt INTERFACE \"/usr/include\" \"/usr/local/include\")\n";
        s += "endif()\n\n";

        s += "find_package(Boost REQUIRED COMPONENTS system thread filesystem)\n";
        s += "if (NOT TARGET Boost::filesystem)\n";
        s += " add_library(Boost::filesystem UNKNOWN IMPORTED)\n";
        s += " set_target_properties(Boost::filesystem PROPERTIES\n";
        s += " IMPORTED_LOCATION \"${Boost_FILESYSTEM_LIBRARY}\"\n";
        s += " INTERFACE_INCLUDE_DIRECTORIES \"${Boost_INCLUDE_DIRS}\")\n";
        s += "endif()\n\n";

        s += "find_package(OpenSSL QUIET)\n";
        s += "if (OpenSSL_FOUND)\n";
        s += " if (NOT TARGET OpenSSL::SSL)\n";
        s += " add_library(OpenSSL::SSL UNKNOWN IMPORTED)\n";
        s += " set_target_properties(OpenSSL::SSL PROPERTIES\n";
        s += " IMPORTED_LOCATION \"${OPENSSL_SSL_LIBRARY}\"\n";
        s += " INTERFACE_INCLUDE_DIRECTORIES \"${OPENSSL_INCLUDE_DIR}\")\n";
        s += " endif()\n";
        s += " if (NOT TARGET OpenSSL::Crypto AND DEFINED OPENSSL_CRYPTO_LIBRARY)\n";
        s += " add_library(OpenSSL::Crypto UNKNOWN IMPORTED)\n";
        s += " set_target_properties(OpenSSL::Crypto PROPERTIES\n";
        s += " IMPORTED_LOCATION \"${OPENSSL_CRYPTO_LIBRARY}\"\n";
        s += " INTERFACE_INCLUDE_DIRECTORIES \"${OPENSSL_INCLUDE_DIR}\")\n";
        s += " endif()\n";
        s += "else()\n";
        s += " if (NOT TARGET OpenSSL::SSL)\n";
        s += " add_library(OpenSSL::SSL INTERFACE IMPORTED)\n";
        s += " endif()\n";
        s += " if (NOT TARGET OpenSSL::Crypto)\n";
        s += " add_library(OpenSSL::Crypto INTERFACE IMPORTED)\n";
        s += " endif()\n";
        s += "endif()\n\n";

        s += "set(VIX_PKG_FOUND FALSE)\n";
        s += "find_package(vix QUIET CONFIG)\n";
        s += "if (vix_FOUND)\n";
        s += " message(STATUS \"Found vix (lowercase) package config\")\n";
        s += " set(VIX_PKG_FOUND TRUE)\n";
        s += "else()\n";
        s += " find_package(Vix QUIET CONFIG)\n";
        s += " if (Vix_FOUND)\n";
        s += " message(STATUS \"Found Vix (legacy) package config\")\n";
        s += " set(VIX_PKG_FOUND TRUE)\n";
        s += " endif()\n";
        s += "endif()\n\n";

        s += "if (NOT VIX_PKG_FOUND)\n";
        s += " message(FATAL_ERROR \"Could not find Vix/vix package config\")\n";
        s += "endif()\n\n";

        s += "set(VIX_CORE_TARGET \"\")\n\n";

        s += "if (TARGET vix::core)\n";
        s += " set(VIX_CORE_TARGET vix::core)\n";
        s += "elseif (TARGET Vix::core)\n";
        s += " set(VIX_CORE_TARGET Vix::core)\n";
        s += "elseif (TARGET vix::vix)\n";
        s += " set(VIX_CORE_TARGET vix::vix)\n";
        s += "elseif (TARGET Vix::vix)\n";
        s += " set(VIX_CORE_TARGET Vix::vix)\n";
        s += "else()\n";
        s += " message(FATAL_ERROR \"No Vix core/main target found (expected vix::core, Vix::core, vix::vix or Vix::vix)\")\n";
        s += "endif()\n\n";

        s += "add_executable(" + exeName + " \"" + cppPath.string() + "\")\n\n";

        s += "target_link_libraries(" + exeName + " PRIVATE\n";
        s += " ${VIX_CORE_TARGET}\n";
        s += " Boost::system\n";
        s += " Boost::thread\n";
        s += " Boost::filesystem\n";
        s += " fmt::fmt\n";
        s += " OpenSSL::SSL\n";
        s += " OpenSSL::Crypto\n";
        s += ")\n\n";

        return s;
    }

    int run_single_cpp(const Options &opt)
    {
        using namespace std;
        namespace fs = std::filesystem;

        const fs::path script = opt.cppFile;
        if (!fs::exists(script))
        {
            error("C++ file not found: " + script.string());
            return 1;
        }

        const string exeName = script.stem().string();

        fs::path scriptsRoot = get_scripts_root();
        fs::create_directories(scriptsRoot);

        fs::path projectDir = scriptsRoot / exeName;
        fs::create_directories(projectDir);

        fs::path cmakeLists = projectDir / "CMakeLists.txt";

        const bool useVixRuntime = script_uses_vix(script);

        // (Re)générer CMakeLists.txt à chaque appel pour refléter
        // l'état actuel du script et de la détection vix::.
        {
            ofstream ofs(cmakeLists);
            ofs << make_script_cmakelists(exeName, script, useVixRuntime);
        }

        fs::path buildDir = projectDir / "build";

        // 🔥 1) Configure projet seulement si nécessaire
        bool needConfigure = true;
        {
            std::error_code ec{};
            if (fs::exists(buildDir / "CMakeCache.txt", ec) && !ec)
            {
                // CMake déjà configuré pour ce script → on peut garder le cache
                // et éviter de relancer `cmake -S . -B build` à chaque run.
                needConfigure = false;
            }
        }

        if (needConfigure)
        {
            // Exécuter CMake configuration silencieusement (une seule fois)
            std::ostringstream oss;
#ifndef _WIN32
            oss << "cd " << quote(projectDir.string())
                << " && cmake -S . -B build 2>&1 >/dev/null";
#else
            oss << "cd " << quote(projectDir.string())
                << " && cmake -S . -B build >nul 2>nul";
#endif
            const std::string cmd = oss.str();

            int code = std::system(cmd.c_str());
            if (code != 0)
            {
                error("Script configure failed.");
                handle_runtime_exit_code(code, "Script configure failed");
                return code;
            }
        }

        // 🔥 2) Compilation — on ne fait plus que `cmake --build` à chaque run
        {
            fs::path logPath = projectDir / "build.log";

            std::ostringstream oss;
            oss << "cd " << quote(projectDir.string())
                << " && cmake --build build --target " << exeName;

            if (opt.jobs > 0)
                oss << " -- -j " << opt.jobs;

#ifndef _WIN32
            // On redirige TOUT (stdout + stderr) vers build.log
            oss << " >" << quote(logPath.string()) << " 2>&1";
#else
            oss.str("");
            oss << "cd " << quote(projectDir.string())
                << " && cmake --build build --target " << exeName;
            if (opt.jobs > 0)
                oss << " -- /m:" << opt.jobs; // style MSBuild
            oss << " >" << quote(logPath.string()) << " 2>&1";
#endif

            const std::string buildCmd = oss.str();
            int code = std::system(buildCmd.c_str());
            if (code != 0)
            {
                // Lire le log et passer au ErrorHandler
                std::ifstream ifs(logPath);
                std::string logContent;

                if (ifs)
                {
                    std::ostringstream logStream;
                    logStream << ifs.rdbuf();
                    logContent = logStream.str();
                }

                if (!logContent.empty())
                {
                    vix::cli::ErrorHandler::printBuildErrors(
                        logContent,
                        script,
                        "Script build failed");
                }
                else
                {
                    // Fallback si pas de log (très rare)
                    error("Script build failed (no compiler log captured).");
                }

                handle_runtime_exit_code(code, "Script build failed");
                return code;
            }
        }

        // 🔥 3) Exécution du binaire
        fs::path exePath = buildDir / exeName;
#ifdef _WIN32
        exePath += ".exe";
#endif

        if (!fs::exists(exePath))
        {
            error("Script binary not found: " + exePath.string());
            return 1;
        }

        int runCode = 0;
        std::string cmdRun;

#ifdef _WIN32
        // Sur Windows, on passe par cmd /C + set
        cmdRun = "cmd /C \"set VIX_STDOUT_MODE=line && " +
                 std::string("\"") + exePath.string() + "\"\"";
#else
        // Sur POSIX, on préfixe avec la variable d'environnement
        cmdRun = "VIX_STDOUT_MODE=line " + quote(exePath.string());
#endif

        runCode = std::system(cmdRun.c_str());

        if (runCode != 0)
        {
            handle_runtime_exit_code(runCode, "Script execution failed");
            return runCode;
        }

        return 0;
    }

    int build_script_executable(const Options &opt, std::filesystem::path &exePath)
    {
        using namespace std;
        namespace fs = std::filesystem;

        const fs::path script = opt.cppFile;
        if (!fs::exists(script))
        {
            error("C++ file not found: " + script.string());
            return 1;
        }

        const string exeName = script.stem().string();

        fs::path scriptsRoot = get_scripts_root();
        fs::create_directories(scriptsRoot);

        fs::path projectDir = scriptsRoot / exeName;
        fs::create_directories(projectDir);

        fs::path cmakeLists = projectDir / "CMakeLists.txt";

        const bool useVixRuntime = script_uses_vix(script);

        // (Re)générer CMakeLists à chaque fois pour refléter les includes / vix::...
        {
            ofstream ofs(cmakeLists);
            ofs << make_script_cmakelists(exeName, script, useVixRuntime);
        }

        fs::path buildDir = projectDir / "build";

        // 🔥 Configure seulement si cache absent
        bool needConfigure = true;
        {
            std::error_code ec{};
            if (fs::exists(buildDir / "CMakeCache.txt", ec) && !ec)
            {
                needConfigure = false;
            }
        }

        if (needConfigure)
        {
            std::ostringstream oss;
#ifndef _WIN32
            oss << "cd " << quote(projectDir.string())
                << " && cmake -S . -B build 2>&1 >/dev/null";
#else
            oss << "cd " << quote(projectDir.string())
                << " && cmake -S . -B build >nul 2>nul";
#endif
            const std::string cmd = oss.str();

            int code = std::system(cmd.c_str());
            if (code != 0)
            {
                error("Script configure failed.");
                handle_runtime_exit_code(code, "Script configure failed");
                return code;
            }
        }

        // 🔥 Build (on ne fait plus que ça à chaque reload)
        fs::path logPath = projectDir / "build.log";

        std::ostringstream oss;
#ifndef _WIN32
        oss << "cd " << quote(projectDir.string())
            << " && cmake --build build --target " << exeName;
        if (opt.jobs > 0)
            oss << " -- -j " << opt.jobs;
        oss << " >" << quote(logPath.string()) << " 2>&1";
#else
        oss << "cd " << quote(projectDir.string())
            << " && cmake --build build --target " << exeName;
        if (opt.jobs > 0)
            oss << " -- /m:" << opt.jobs;
        oss << " >" << quote(logPath.string()) << " 2>&1";
#endif

        const std::string buildCmd = oss.str();
        int code = std::system(buildCmd.c_str());
        if (code != 0)
        {
            // Lire le log et passer au ErrorHandler
            std::ifstream ifs(logPath);
            std::string logContent;

            if (ifs)
            {
                std::ostringstream logStream;
                logStream << ifs.rdbuf();
                logContent = logStream.str();
            }

            if (!logContent.empty())
            {
                vix::cli::ErrorHandler::printBuildErrors(
                    logContent,
                    script,
                    "Script build failed");
            }
            else
            {
                error("Script build failed (no compiler log captured).");
            }

            handle_runtime_exit_code(code, "Script build failed");
            return code;
        }

        exePath = buildDir / exeName;
#ifdef _WIN32
        exePath += ".exe";
#endif

        if (!fs::exists(exePath))
        {
            error("Script binary not found: " + exePath.string());
            return 1;
        }

        return 0;
    }

    int run_single_cpp_watch(const Options &opt)
    {
        using namespace std::chrono_literals;
        namespace fs = std::filesystem;
        using Clock = std::chrono::steady_clock;

        const fs::path script = opt.cppFile;
        if (!fs::exists(script))
        {
            error("C++ file not found: " + script.string());
            return 1;
        }

        std::error_code ec{};
        auto lastWrite = fs::last_write_time(script, ec);
        if (ec)
        {
            error("Unable to read last_write_time for: " + script.string());
            return 1;
        }

        const bool usesVixRuntime = script_uses_vix(script);
        const bool hasForceServer = opt.forceServerLike;
        const bool hasForceScript = opt.forceScriptLike;
        bool dynamicServerLike = usesVixRuntime;

        auto final_is_server = [&](bool runtimeGuess) -> bool
        {
            if (hasForceServer && hasForceScript)
            {
                return true;
            }
            if (hasForceServer)
                return true;
            if (hasForceScript)
                return false;
            return runtimeGuess;
        };

        auto kind_label = [&](bool runtimeGuess) -> std::string
        {
            return final_is_server(runtimeGuess) ? "dev server" : "script";
        };

        info("Watcher Process started (hot reload).");
        hint("Watching: " + script.string());

#ifdef _WIN32
        // 🔁 Fallback simple sur Windows : on garde run_single_cpp
        while (true)
        {
            const auto start = Clock::now();
            int code = run_single_cpp(opt);
            const auto end = Clock::now();
            const auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            // Heuristique durée seulement si pas forcé
            if (!hasForceServer && !hasForceScript)
            {
                bool runtimeGuess = dynamicServerLike;
                const bool longLived = (ms >= 500);

                if (longLived && !runtimeGuess)
                    runtimeGuess = true;
                if (!longLived && runtimeGuess && code == 0)
                    runtimeGuess = false;

                dynamicServerLike = runtimeGuess;
            }

            if (code != 0)
            {
                const std::string label = kind_label(dynamicServerLike);
                error("Last " + label + " run failed (exit code " +
                      std::to_string(code) + ").");
                hint("Fix the errors, save the file, and Vix will rebuild automatically.");
            }

            // 🕵️ Ici on ne spam plus “Watching...” : on attend juste un changement
            for (;;)
            {
                std::this_thread::sleep_for(500ms);

                auto nowWrite = fs::last_write_time(script, ec);
                if (ec)
                {
                    error("Error reading last_write_time during watch loop.");
                    return 1;
                }

                if (nowWrite != lastWrite)
                {
                    lastWrite = nowWrite;
                    // CLEAR + bannière de restart comme Deno
                    print_watch_restart_banner(script);
                    break; // relancer run_single_cpp
                }
            }
        }

        return 0;

#else
        // Implémentation POSIX avec fork/exec + kill(SIGINT)
        while (true)
        {
            // 1) Build exécutable (cache CMake réutilisé → rapide)
            fs::path exePath;
            int buildCode = build_script_executable(opt, exePath);
            if (buildCode != 0)
            {
                const std::string label = kind_label(dynamicServerLike);

                error("Last " + label + " build failed (exit code " +
                      std::to_string(buildCode) + ").");
                hint("Fix the errors, save the file, and Vix will rebuild automatically.");

                for (;;)
                {
                    std::this_thread::sleep_for(500ms);

                    auto nowWrite = fs::last_write_time(script, ec);
                    if (ec)
                    {
                        error("Error reading last_write_time during watch loop.");
                        return 1;
                    }

                    if (nowWrite != lastWrite)
                    {
                        lastWrite = nowWrite;
                        // CLEAR + bannière de restart
                        print_watch_restart_banner(script);
                        break;
                    }
                }
                continue;
            }

            // 2) Lancer le process (serveur ou script) dans un enfant
            const auto childStart = Clock::now();

            pid_t pid = fork();
            if (pid < 0)
            {
                error("Failed to fork() for dev process.");
                return 1;
            }

            if (pid == 0)
            {
                // ===== ENFANT =====
                ::setenv("VIX_STDOUT_MODE", "line", 1);

                const std::string exeStr = exePath.string();
                const char *argv0 = exeStr.c_str();

                execl(argv0, argv0, (char *)nullptr);
                _exit(127); // si execl échoue
            }

            // ===== PARENT =====
            bool needRestart = false;
            bool running = true;

            {
                const bool isServer = final_is_server(dynamicServerLike);
                const std::string kind = isServer ? "Dev server" : "Script";

                info(std::string("🏃 ") + kind +
                     " started (pid=" + std::to_string(pid) + ")");
            }

            while (running)
            {
                std::this_thread::sleep_for(300ms);

                // 2.a Vérifier si le script a changé → déclenche un restart
                auto nowWrite = fs::last_write_time(script, ec);
                if (!ec && nowWrite != lastWrite)
                {
                    lastWrite = nowWrite;

                    // CLEAR + bannière de restart
                    print_watch_restart_banner(script);

                    needRestart = true;

                    if (kill(pid, SIGINT) != 0)
                    {
                        // peut-être déjà fini, on ignore
                    }
                }

                // 2.b Vérifier si le process est terminé
                int status = 0;
                pid_t r = waitpid(pid, &status, WNOHANG);
                if (r == pid)
                {
                    running = false;

                    const auto childEnd = Clock::now();
                    const auto ms =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            childEnd - childStart)
                            .count();

                    int exitCode = 0;
                    if (WIFEXITED(status))
                        exitCode = WEXITSTATUS(status);
                    else if (WIFSIGNALED(status))
                        exitCode = 128 + WTERMSIG(status);

                    // Mise à jour dynamique de la classification (si pas forcée)
                    if (!hasForceServer && !hasForceScript)
                    {
                        bool runtimeGuess = dynamicServerLike;
                        const bool longLived = (ms >= 500);

                        if (longLived && !runtimeGuess)
                            runtimeGuess = true;
                        if (!longLived && runtimeGuess && exitCode == 0)
                            runtimeGuess = false;

                        dynamicServerLike = runtimeGuess;
                    }

                    const bool isServer = final_is_server(dynamicServerLike);
                    const std::string label = isServer ? "dev server" : "script";

                    if (needRestart)
                    {
                        break; // rebuild + relaunch
                    }

                    // Pas de spam en cas de succès : comme Deno, on reste silencieux
                    if (exitCode != 0)
                    {
                        error(label + " exited with code " +
                              std::to_string(exitCode) +
                              " (lifetime ~" + std::to_string(ms) + "ms).");
                    }

                    // Après sortie “normale”, on reste en attente silencieuse jusqu’à la prochaine modif
                    for (;;)
                    {
                        std::this_thread::sleep_for(500ms);

                        auto now2 = fs::last_write_time(script, ec);
                        if (ec)
                        {
                            error("Error reading last_write_time during post-exit watch.");
                            return exitCode;
                        }

                        if (now2 != lastWrite)
                        {
                            lastWrite = now2;
                            // CLEAR + bannière de restart
                            print_watch_restart_banner(script);
                            break;
                        }
                    }

                    break; // retour en haut du while(true) → rebuild + relaunch
                }
            }
        }

        return 0;
#endif
    }

    // Petit helper: timestamp "global" du projet (max last_write_time)
    static fs::file_time_type compute_project_stamp(const fs::path &root, std::error_code &outEc)
    {
        using ftime = fs::file_time_type;
        outEc.clear();

        ftime latest{}; // par défaut

        std::error_code ec;
        if (!fs::exists(root, ec))
        {
            outEc = ec;
            return latest;
        }

        fs::recursive_directory_iterator it(root, ec), end;
        if (ec)
        {
            outEc = ec;
            return latest;
        }

        for (; it != end; it.increment(ec))
        {
            if (ec)
                break;

            const fs::path &p = it->path();
            const std::string name = p.filename().string();

            // On ignore les dossiers de build / meta
            if (name == ".git" ||
                name == "build" ||
                name == ".vix-scripts" ||
                name == "cmake-build-debug" ||
                name == "cmake-build-release")
            {
                if (fs::is_directory(p))
                    it.disable_recursion_pending();
                continue;
            }

            auto t = fs::last_write_time(p, ec);
            if (ec)
                continue;

            if (t > latest)
                latest = t;
        }

        outEc.clear();
        return latest;
    }

    int run_project_watch(const Options &opt, const std::filesystem::path &projectDir)
    {
#ifndef _WIN32
        using Clock = std::chrono::steady_clock;
        namespace fs = std::filesystem;
        using namespace vix::cli::style;

        const fs::path buildDir = projectDir / "build-dev";

        std::error_code ec;
        fs::create_directories(buildDir, ec);
        if (ec)
        {
            error("Unable to create dev build directory: " + buildDir.string() +
                  " (" + ec.message() + ")");
            return 1;
        }

        // Timestamp global du projet : CMakeLists.txt + src/
        auto compute_timestamp = [&](std::error_code &outEc) -> fs::file_time_type
        {
            using ftime = fs::file_time_type;
            outEc.clear();

            ftime latest = ftime::min();

            auto touch = [&](const fs::path &p)
            {
                std::error_code e;
                if (!fs::exists(p, e) || e)
                    return;
                auto t = fs::last_write_time(p, e);
                if (!e && t > latest)
                    latest = t;
            };

            touch(projectDir / "CMakeLists.txt");

            fs::path srcDir = projectDir / "src";
            if (fs::exists(srcDir, outEc) && !outEc)
            {
                for (auto it = fs::recursive_directory_iterator(
                         srcDir,
                         fs::directory_options::skip_permission_denied,
                         outEc);
                     !outEc && it != fs::recursive_directory_iterator();
                     ++it)
                {
                    if (it->is_regular_file())
                        touch(it->path());
                }
            }

            return latest;
        };

        std::error_code tsEc;
        auto lastStamp = compute_timestamp(tsEc);
        if (tsEc)
        {
            hint("Unable to compute initial timestamp for dev watch: " + tsEc.message());
        }

        info("Watcher Process started (project hot reload).");
        hint("Watching project: " + projectDir.string());
        hint("Press Ctrl+C to stop dev mode.");

        while (true)
        {
            // 1) Configure si pas de cache
            if (!has_cmake_cache(buildDir))
            {
                info("Configuring project for dev mode (build-dev/).");

                std::ostringstream oss;
                oss << "cd " << quote(buildDir.string()) << " && cmake ..";
                const std::string cmd = oss.str();

                const int code = run_cmd_live_filtered(cmd, "Configuring project (dev mode)");
                if (code != 0)
                {
                    error("CMake configure failed for dev mode (build-dev/, code " +
                          std::to_string(code) + ").");
                    hint("Check your CMakeLists.txt or run the command manually:");
                    step("  cd " + buildDir.string());
                    step("  cmake ..");
                    return code != 0 ? code : 4;
                }

                success("Dev configure completed (build-dev/).");
            }

            // 2) Build
            {
                info("Building project (dev mode, build-dev/).");

                std::ostringstream oss;
                oss << "cd " << quote(buildDir.string()) << " && cmake --build .";
                if (opt.jobs > 0)
                    oss << " -j " << opt.jobs;

                const std::string cmd = oss.str();

                int code = 0;
                std::string buildLog = run_and_capture_with_code(cmd, code);

                if (code != 0)
                {
                    if (!buildLog.empty())
                    {
                        vix::cli::ErrorHandler::printBuildErrors(
                            buildLog,
                            buildDir,
                            "Build failed in dev mode (build-dev/)");
                    }
                    else
                    {
                        error("Build failed in dev mode (build-dev/, code " +
                              std::to_string(code) + ").");
                    }

                    hint("Fix the errors, save your files, and Vix will rebuild automatically.");

                    while (true)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));

                        std::error_code loopEc;
                        auto nowStamp = compute_timestamp(loopEc);
                        if (!loopEc && nowStamp != lastStamp)
                        {
                            lastStamp = nowStamp;
                            print_watch_restart_banner(projectDir);
                            break;
                        }
                    }

                    continue;
                }

                if (!buildLog.empty() && has_real_build_work(buildLog))
                    std::cout << buildLog;

                success("Build completed (dev mode).");
            }

            // 3) Lancer le binaire
            const std::string exeName = projectDir.filename().string();
            fs::path exePath = buildDir / exeName;

            if (!fs::exists(exePath))
            {
                error("Dev executable not found in build-dev/: " + exePath.string());
                hint("Make sure your CMakeLists.txt defines an executable named '" +
                     exeName + "'.");
                return 1;
            }

            const auto childStart = Clock::now();

            pid_t pid = ::fork();
            if (pid < 0)
            {
                error("Failed to fork() for dev process.");
                return 1;
            }

            if (pid == 0)
            {
                ::chdir(buildDir.string().c_str());
                ::setenv("VIX_STDOUT_MODE", "line", 1);

                const std::string exeStr = exePath.string();
                const char *argv0 = exeStr.c_str();

                execl(argv0, argv0, (char *)nullptr);
                _exit(127);
            }

            info("🏃 Dev server started (pid=" + std::to_string(pid) + ")");

            bool needRestart = false;
            bool running = true;

            while (running)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));

                // Watch files
                std::error_code loopEc;
                auto nowStamp = compute_timestamp(loopEc);
                if (!loopEc && nowStamp != lastStamp)
                {
                    lastStamp = nowStamp;
                    print_watch_restart_banner(projectDir);
                    needRestart = true;

                    if (::kill(pid, SIGINT) != 0)
                    {
                        // peut-être déjà mort
                    }
                }

                int status = 0;
                pid_t r = ::waitpid(pid, &status, WNOHANG);
                if (r == pid)
                {
                    running = false;

                    const auto childEnd = Clock::now();
                    const auto ms =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            childEnd - childStart)
                            .count();

                    int exitCode = 0;
                    if (WIFEXITED(status))
                        exitCode = WEXITSTATUS(status);
                    else if (WIFSIGNALED(status))
                        exitCode = 128 + WTERMSIG(status);

                    if (!needRestart)
                    {
                        if (exitCode != 0)
                        {
                            error("Dev server exited with code " +
                                  std::to_string(exitCode) +
                                  " (lifetime ~" + std::to_string(ms) + "ms).");
                        }
                        else
                        {
                            success("Dev server stopped cleanly (lifetime ~" +
                                    std::to_string(ms) + "ms).");
                        }
                        return exitCode;
                    }
                }
            }
        }

        return 0;
#else
        (void)opt;
        (void)projectDir;
        error("run_project_watch is not implemented on Windows.");
        return 1;
#endif
    }

} // namespace vix::commands::RunCommand::detail