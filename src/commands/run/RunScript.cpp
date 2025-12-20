#include "vix/cli/commands/run/RunDetail.hpp"
#include <vix/cli/errors/RawLogDetectors.hpp>
#include <vix/cli/Style.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <chrono>

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

        inline std::string bool01(bool v)
        {
            return v ? "1" : "0";
        }

        inline std::string sanitizer_mode_string(const Options &opt)
        {
            if (opt.enableUbsanOnly)
                return "ubsan";
            return "asan_ubsan";
        }

        inline bool want_sanitizers(const Options &opt)
        {
            return opt.enableSanitizers || opt.enableUbsanOnly;
        }

        inline std::string make_script_config_signature(bool useVixRuntime, const Options &opt)
        {
            std::string sig;
            sig.reserve(64);
            sig += "useVix=" + bool01(useVixRuntime);
            sig += ";san=" + bool01(want_sanitizers(opt));
            sig += ";mode=" + sanitizer_mode_string(opt);
            return sig;
        }

        inline std::string read_text_file_or_empty(const fs::path &p)
        {
            std::ifstream ifs(p);
            if (!ifs)
                return {};
            std::ostringstream oss;
            oss << ifs.rdbuf();
            return oss.str();
        }

        inline bool write_text_file(const fs::path &p, const std::string &text)
        {
            std::ofstream ofs(p, std::ios::trunc);
            if (!ofs)
                return false;
            ofs << text;
            return static_cast<bool>(ofs);
        }

#ifndef _WIN32
        inline void apply_sanitizer_env_if_needed(const Options &opt)
        {
            if (!want_sanitizers(opt))
                return;

            // UBSan
            ::setenv("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1:color=never", 1);

            if (!opt.enableUbsanOnly)
            {
                // ASan
                ::setenv(
                    "ASAN_OPTIONS",
                    "abort_on_error=1:"
                    "detect_leaks=1:"
                    "symbolize=1:"
                    "allocator_may_return_null=1:"
                    "fast_unwind_on_malloc=0:"
                    "strict_init_order=1:"
                    "check_initialization_order=1:"
                    "color=never:"
                    "quiet=1",
                    1);
            }
        }
#endif
    } // namespace

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
        s.reserve(6200);

        auto q = [](const std::string &p)
        {
            std::string out = "\"";
            for (char c : p)
            {
                if (c == '\\')
                    out += "\\\\";
                else if (c == '"')
                    out += "\\\"";
                else
                    out += c;
            }
            out += "\"";
            return out;
        };

        s += "cmake_minimum_required(VERSION 3.20)\n";
        s += "project(" + exeName + " LANGUAGES CXX)\n\n";

        // Standard
        s += "set(CMAKE_CXX_STANDARD 20)\n";
        s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
        s += "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

        // Default build type
        s += "if (NOT CMAKE_BUILD_TYPE)\n";
        s += "  set(CMAKE_BUILD_TYPE Debug CACHE STRING \"Build type\" FORCE)\n";
        s += "endif()\n\n";

        // Options
        s += "option(VIX_ENABLE_SANITIZERS \"Enable sanitizers (dev only)\" OFF)\n";
        s += "set(VIX_SANITIZER_MODE \"asan_ubsan\" CACHE STRING \"Sanitizer mode: asan_ubsan or ubsan\")\n";
        s += "set_property(CACHE VIX_SANITIZER_MODE PROPERTY STRINGS asan_ubsan ubsan)\n";
        s += "option(VIX_ENABLE_LIBCXX_ASSERTS \"Enable libstdc++ debug mode (_GLIBCXX_ASSERTIONS/_GLIBCXX_DEBUG)\" OFF)\n";
        s += "option(VIX_ENABLE_HARDENING \"Enable extra hardening flags (non-MSVC)\" OFF)\n";
        s += "option(VIX_USE_ORM \"Enable Vix ORM (requires vix::orm in install)\" OFF)\n\n";

        // Executable
        s += "add_executable(" + exeName + " " + q(cppPath.string()) + ")\n\n";

        // Warnings
        s += "if (MSVC)\n";
        s += "  target_compile_options(" + exeName + " PRIVATE /W4 /permissive- /EHsc)\n";
        s += "  target_compile_definitions(" + exeName + " PRIVATE _CRT_SECURE_NO_WARNINGS)\n";
        s += "else()\n";
        s += "  target_compile_options(" + exeName + " PRIVATE\n";
        s += "    -Wall -Wextra -Wpedantic\n";
        s += "    -Wshadow -Wconversion -Wsign-conversion\n";
        s += "    -Wformat=2 -Wnull-dereference\n";
        s += "  )\n";
        s += "endif()\n\n";

        // Better backtraces
        s += "if (NOT MSVC)\n";
        s += "  target_compile_options(" + exeName + " PRIVATE -fno-omit-frame-pointer)\n";
        s += "  if (UNIX AND NOT APPLE)\n";
        s += "    target_link_options(" + exeName + " PRIVATE -rdynamic)\n";
        s += "  endif()\n";
        s += "endif()\n\n";

        // libstdc++ assertions
        s += "if (VIX_ENABLE_LIBCXX_ASSERTS AND NOT MSVC)\n";
        s += "  target_compile_definitions(" + exeName + " PRIVATE _GLIBCXX_ASSERTIONS)\n";
        s += "endif()\n\n";

        // Hardening
        s += "if (VIX_ENABLE_HARDENING AND NOT MSVC)\n";
        s += "  target_compile_options(" + exeName + " PRIVATE -D_FORTIFY_SOURCE=2)\n";
        s += "  target_link_options(" + exeName + " PRIVATE -Wl,-z,relro -Wl,-z,now)\n";
        s += "endif()\n\n";

        // Link libs if standalone
        if (!useVixRuntime)
        {
            s += "if (UNIX AND NOT APPLE)\n";
            s += "  target_link_libraries(" + exeName + " PRIVATE pthread dl)\n";
            s += "endif()\n\n";
        }
        else
        {
            s += "# Prefer lowercase package, fallback to legacy Vix\n";
            s += "find_package(vix QUIET CONFIG)\n";
            s += "if (NOT vix_FOUND)\n";
            s += "  find_package(Vix CONFIG REQUIRED)\n";
            s += "endif()\n\n";

            s += "# Pick main target (umbrella preferred)\n";
            s += "set(VIX_MAIN_TARGET \"\")\n";
            s += "if (TARGET vix::vix)\n";
            s += "  set(VIX_MAIN_TARGET vix::vix)\n";
            s += "elseif (TARGET Vix::vix)\n";
            s += "  set(VIX_MAIN_TARGET Vix::vix)\n";
            s += "elseif (TARGET vix::core)\n";
            s += "  set(VIX_MAIN_TARGET vix::core)\n";
            s += "elseif (TARGET Vix::core)\n";
            s += "  set(VIX_MAIN_TARGET Vix::core)\n";
            s += "else()\n";
            s += "  message(FATAL_ERROR \"No Vix target found (vix::vix/Vix::vix/vix::core/Vix::core)\")\n";
            s += "endif()\n\n";

            s += "target_link_libraries(" + exeName + " PRIVATE ${VIX_MAIN_TARGET})\n\n";

            s += "# Optional ORM\n";
            s += "if (VIX_USE_ORM)\n";
            s += "  if (TARGET vix::orm)\n";
            s += "    target_link_libraries(" + exeName + " PRIVATE vix::orm)\n";
            s += "    target_compile_definitions(" + exeName + " PRIVATE VIX_USE_ORM=1)\n";
            s += "  else()\n";
            s += "    message(FATAL_ERROR \"VIX_USE_ORM=ON but vix::orm target is not available in this Vix install\")\n";
            s += "  endif()\n";
            s += "endif()\n\n";
        }

        // Sanitizers (mode-aware)
        s += "if (VIX_ENABLE_SANITIZERS AND NOT MSVC)\n";
        s += "  if (VIX_SANITIZER_MODE STREQUAL \"ubsan\")\n";
        s += "    message(STATUS \"Sanitizers: UBSan enabled\")\n";
        s += "    target_compile_options(" + exeName + " PRIVATE\n";
        s += "      -O0 -g3\n";
        s += "      -fno-omit-frame-pointer\n";
        s += "      -fsanitize=undefined\n";
        s += "      -fno-sanitize-recover=all\n";
        s += "    )\n";
        s += "    target_link_options(" + exeName + " PRIVATE -fsanitize=undefined)\n";
        s += "  else()\n";
        s += "    message(STATUS \"Sanitizers: ASan+UBSan enabled\")\n";
        s += "    target_compile_options(" + exeName + " PRIVATE\n";
        s += "      -O1 -g3\n";
        s += "      -fno-omit-frame-pointer\n";
        s += "      -fsanitize=address,undefined\n";
        s += "      -fno-sanitize-recover=all\n";
        s += "    )\n";
        s += "    target_link_options(" + exeName + " PRIVATE -fsanitize=address,undefined)\n";
        s += "    target_compile_definitions(" + exeName + " PRIVATE VIX_ASAN_QUIET=1)\n";
        s += "  endif()\n";
        s += "endif()\n\n";

        // Always keep some debug info on Linux
        s += "if (UNIX AND NOT APPLE)\n";
        s += "  target_compile_options(" + exeName + " PRIVATE -g)\n";
        s += "endif()\n";

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

        {
            ofstream ofs(cmakeLists);
            ofs << make_script_cmakelists(exeName, script, useVixRuntime);
        }

        fs::path buildDir = projectDir / "build";
        const fs::path sigFile = projectDir / ".vix-config.sig";

        const std::string sig = make_script_config_signature(useVixRuntime, opt);

        bool needConfigure = true;
        {
            std::error_code ec{};
            if (fs::exists(buildDir / "CMakeCache.txt", ec) && !ec)
            {
                const std::string oldSig = read_text_file_or_empty(sigFile);
                if (!oldSig.empty() && oldSig == sig)
                    needConfigure = false;
            }
        }

        if (needConfigure)
        {
            std::ostringstream oss;

            oss << "cd " << quote(projectDir.string()) << " && cmake -S . -B build";

            if (want_sanitizers(opt))
            {
                oss << " -DVIX_ENABLE_SANITIZERS=ON"
                    << " -DVIX_SANITIZER_MODE=" << sanitizer_mode_string(opt);
            }
            else
            {
                oss << " -DVIX_ENABLE_SANITIZERS=OFF";
            }

#ifndef _WIN32
            oss << " 2>&1 >/dev/null";
#else
            oss << " >nul 2>nul";
#endif

            const std::string cmd = oss.str();
            int code = std::system(cmd.c_str());
            if (code != 0)
            {
                error("Script configure failed.");
                handle_runtime_exit_code(code, "Script configure failed");
                return code;
            }

            (void)write_text_file(sigFile, sig);
        }

        // Build
        {
            fs::path logPath = projectDir / "build.log";

            std::ostringstream oss;
            oss << "cd " << quote(projectDir.string())
                << " && cmake --build build --target " << exeName;

            if (opt.jobs > 0)
                oss << " -- -j " << opt.jobs;

#ifndef _WIN32
            oss << " >" << quote(logPath.string()) << " 2>&1";
#else
            oss << " >" << quote(logPath.string()) << " 2>&1";
#endif

            const std::string buildCmd = oss.str();
            int code = std::system(buildCmd.c_str());
            if (code != 0)
            {
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
        }

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
        cmdRun = "cmd /C \"set VIX_STDOUT_MODE=line && " +
                 std::string("\"") + exePath.string() + "\"\"";
#else
        cmdRun = "VIX_STDOUT_MODE=line " + quote(exePath.string());
#endif

#ifndef _WIN32
        // Apply runtime env (more reliable than trying to set it in CMake)
        apply_sanitizer_env_if_needed(opt);

        auto rr = run_cmd_live_filtered_capture(cmdRun, "Running script", true);
        runCode = normalize_exit_code(rr.exitCode);

        if (runCode != 0)
        {
            const std::string runtimeLog = rr.stdoutText + "\n" + rr.stderrText;

            vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
                runtimeLog, script, "Script execution failed");

            handle_runtime_exit_code(runCode, "Script execution failed");
            return runCode;
        }
#else
        runCode = std::system(cmdRun.c_str());
        runCode = normalize_exit_code(runCode);
        if (runCode != 0)
        {
            handle_runtime_exit_code(runCode, "Script execution failed");
            return runCode;
        }
#endif

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

        {
            ofstream ofs(cmakeLists);
            ofs << make_script_cmakelists(exeName, script, useVixRuntime);
        }

        fs::path buildDir = projectDir / "build";
        const fs::path sigFile = projectDir / ".vix-config.sig";

        const std::string sig = make_script_config_signature(useVixRuntime, opt);

        bool needConfigure = true;
        {
            std::error_code ec{};
            if (fs::exists(buildDir / "CMakeCache.txt", ec) && !ec)
            {
                const std::string oldSig = read_text_file_or_empty(sigFile);
                if (!oldSig.empty() && oldSig == sig)
                    needConfigure = false;
            }
        }

        if (needConfigure)
        {
            std::ostringstream oss;

            oss << "cd " << quote(projectDir.string()) << " && cmake -S . -B build";

            if (want_sanitizers(opt))
            {
                oss << " -DVIX_ENABLE_SANITIZERS=ON"
                    << " -DVIX_SANITIZER_MODE=" << sanitizer_mode_string(opt);
            }
            else
            {
                oss << " -DVIX_ENABLE_SANITIZERS=OFF";
            }

#ifndef _WIN32
            oss << " 2>&1 >/dev/null";
#else
            oss << " >nul 2>nul";
#endif

            const std::string cmd = oss.str();

            int code = std::system(cmd.c_str());
            if (code != 0)
            {
                error("Script configure failed.");
                handle_runtime_exit_code(code, "Script configure failed");
                return code;
            }

            (void)write_text_file(sigFile, sig);
        }

        // Build
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
        // Fallback simple sur Windows : on garde run_single_cpp
        while (true)
        {
            const auto start = Clock::now();
            int code = run_single_cpp(opt);
            const auto end = Clock::now();
            const auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

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

            // Ici on ne spam plus “Watching...” : on attend juste un changement
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

                apply_sanitizer_env_if_needed(opt);

                const std::string exeStr = exePath.string();
                const char *argv0 = exeStr.c_str();

                execl(argv0, argv0, (char *)nullptr);
                _exit(127);
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
                // chdir
                if (::chdir(buildDir.string().c_str()) != 0)
                {
                    // (si tu as un logger dans ce fichier, utilise-le, sinon stderr)
                    std::cerr << "[vix][run] chdir failed: " << std::strerror(errno) << "\n";
                    _exit(127);
                }

                // setenv
                if (::setenv("VIX_STDOUT_MODE", "line", 1) != 0)
                {
                    std::cerr << "[vix][run] setenv failed: " << std::strerror(errno) << "\n";
                    _exit(127);
                }

                const std::string exeStr = exePath.string();
                const char *argv0 = exeStr.c_str();

                // execl (si ça retourne, c'est un échec)
                ::execl(argv0, argv0, (char *)nullptr);

                std::cerr << "[vix][run] execl failed: " << std::strerror(errno) << "\n";
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