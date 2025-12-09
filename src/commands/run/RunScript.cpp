#include "vix/cli/commands/run/RunDetail.hpp"
#include <vix/cli/Style.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace vix::cli::style;

namespace vix::commands::RunCommand::detail
{
    namespace fs = std::filesystem;

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

    /// Run a single C++ source file in “script mode”.
    ///
    /// This function powers the command:
    ///     vix run file.cpp
    ///
    /// It provides a smooth and IDE-like experience similar to running a script,
    /// while still using a full CMake-based build pipeline under the hood.
    ///
    /// High-level workflow:
    /// --------------------
    /// 1. Validate that the input file exists.
    /// 2. Create a dedicated temporary build directory inside:
    ///        ~/.vix-scripts/<filename>/
    /// 3. Generate a minimal CMakeLists.txt adapted for:
    ///        - pure C++ files
    ///        - or scripts using the Vix runtime (detected automatically)
    /// 4. Run CMake configuration silently.
    /// 5. Build the target using CMake, capturing *all* compiler and linker output
    ///    into a log file.
    /// 6. If the compilation fails:
    ///        - Read the captured log
    ///        - Pass it to ErrorHandler::printBuildErrors(), which:
    ///            • parses Clang/GCC errors (file, line, column, message)
    ///            • detects friendly “special cases”
    ///              (missing std::cout, missing semicolon, ambiguous overloads,
    ///               use-after-move, missing #include, etc.)
    ///            • prints colored, pedagogical error messages
    ///        - The function then returns the compiler’s exit code.
    ///
    /// 7. If the build succeeds:
    ///        - Locate the executable produced by CMake
    ///        - Run it directly
    ///        - Forward the program’s exit code (including crashes)
    ///
    /// Error handling:
    /// ---------------
    /// • If the compiler fails → ErrorHandler is used for clean, colored output.
    /// • If the linker fails   → full log is parsed, and linker diagnostics are
    ///   printed the same way (undefined symbols, missing files, ODR violations,
    ///   memory / ASan reports, etc.).
    /// • If no log is captured → a fallback message is printed.
    ///
    /// Why this exists:
    /// ----------------
    /// It turns Vix into an educational C++ runtime:
    ///    - CMake complexity is hidden.
    ///    - The user simply runs:  vix run file.cpp
    ///    - Compiler errors are transformed into friendly hints.
    ///
    /// This is the foundation of the “Vix Script Mode” experience.
    ///
    /// \return
    ///    0  → success
    ///    >0 → build or runtime error (compiler, linker, or executed program)
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
        path projectDir = scriptsRoot / exeName;
        create_directories(projectDir);
        path cmakeLists = projectDir / "CMakeLists.txt";

        bool useVixRuntime = script_uses_vix(script);

        {
            ofstream ofs(cmakeLists);
            ofs << make_script_cmakelists(exeName, script, useVixRuntime);
        }

        // Exécuter CMake configuration silencieusement
        {
            std::ostringstream oss;
            oss << "cd " << quote(projectDir.string()) << " && cmake -S . -B build 2>&1 >/dev/null";
#ifdef _WIN32
            oss.str(""); // Réinitialiser pour Windows
            oss << "cd " << quote(projectDir.string()) << " && cmake -S . -B build >nul 2>nul";
#endif
            int code = std::system(oss.str().c_str());
            if (code != 0)
            {
                error("Script configure failed.");
                return code;
            }
        }

        // Exécuter la compilation en capturant le log pour un affichage propre
        {
            path buildDir = projectDir / "build";
            path logPath = projectDir / "build.log";

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

            int code = std::system(oss.str().c_str());
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

                // On garde ta gestion d’exit code pour la cohérence CLI
                handle_runtime_exit_code(code, "Script build failed");
                return code;
            }
        }

        path exePath = projectDir / "build" / exeName;
#ifdef _WIN32
        exePath += ".exe";
#endif

        if (!fs::exists(exePath))
        {
            error("Script binary not found: " + exePath.string());
            return 1;
        }

        // Exécuter le binaire directement, sans messages de configuration
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
} // namespace vix::commands::RunCommand::detail