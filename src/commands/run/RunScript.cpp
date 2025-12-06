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

    // Détection simple : est-ce que le script utilise Vix ?
    bool script_uses_vix(const std::filesystem::path &cppPath)
    {
        std::ifstream ifs(cppPath);
        if (!ifs)
            return false;

        std::string line;
        while (std::getline(ifs, line))
        {
            // Si le code utilise explicitement le namespace vix::
            if (line.find("vix::") != std::string::npos ||
                line.find("Vix::") != std::string::npos)
            {
                return true;
            }

            // Si la ligne n'est pas un include, on passe
            if (line.find("#include") == std::string::npos)
                continue;

            // Tout include qui contient "vix" ou "Vix" est considéré comme script Vix
            if (line.find("vix") != std::string::npos ||
                line.find("Vix") != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    std::filesystem::path get_scripts_root()
    {
        auto cwd = std::filesystem::current_path();
        return cwd / ".vix-scripts";
    }

    std::string make_script_cmakelists(const std::string &exeName,
                                       const std::filesystem::path &cppPath,
                                       bool useVixRuntime)
    {
        std::string s;

        s += "cmake_minimum_required(VERSION 3.20)\n";
        s += "project(" + exeName + " LANGUAGES CXX)\n\n";

        // ------------------------------------------------------------------
        // 1) Build type + standard + flags (on force Release par défaut)
        // ------------------------------------------------------------------
        s += "if (NOT CMAKE_BUILD_TYPE)\n";
        s += "    set(CMAKE_BUILD_TYPE Release CACHE STRING \"Build type\" FORCE)\n";
        s += "endif()\n\n";

        s += "set(CMAKE_CXX_STANDARD 20)\n";
        s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

        s += "set(GLOBAL_CXX_FLAGS \"-Wall -Wextra -Wshadow\")\n";
        s += "set(CMAKE_CXX_FLAGS_RELEASE \"${CMAKE_CXX_FLAGS_RELEASE} ${GLOBAL_CXX_FLAGS} -O3 -DNDEBUG\")\n";
        s += "set(CMAKE_CXX_FLAGS_DEBUG   \"${CMAKE_CXX_FLAGS_DEBUG}   ${GLOBAL_CXX_FLAGS} -O0 -g\")\n\n";

        if (!useVixRuntime)
        {
            // ------------------------------------------------------
            // Mode "script C++ classique" (sans Vix)
            // ------------------------------------------------------
            s += "add_executable(" + exeName + " \"" + cppPath.string() + "\")\n\n";

            // Sur Linux, on link souvent pthread/dl pour être safe
            s += "if (UNIX AND NOT APPLE)\n";
            s += "  target_link_libraries(" + exeName + " PRIVATE pthread dl)\n";
            s += "endif()\n\n";

            s += "add_custom_target(run\n";
            s += "  COMMAND $<TARGET_FILE:" + exeName + ">\n";
            s += "  DEPENDS " + exeName + "\n";
            s += "  USES_TERMINAL\n";
            s += ")\n";

            return s;
        }

        // ------------------------------------------------------
        // Mode "script Vix" (runtime HTTP/WebSocket/etc.)
        // ------------------------------------------------------

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

        // ===== Vix (on préfère vix::core, sinon on fallback) =====
        s += "set(VIX_PKG_FOUND FALSE)\n";
        s += "find_package(vix QUIET CONFIG)\n";
        s += "if (vix_FOUND)\n";
        s += "  message(STATUS \"Found vix (lowercase) package config\")\n";
        s += "  set(VIX_PKG_FOUND TRUE)\n";
        s += "else()\n";
        s += "  find_package(Vix QUIET CONFIG)\n";
        s += "  if (Vix_FOUND)\n";
        s += "    message(STATUS \"Found Vix (legacy) package config\")\n";
        s += "    set(VIX_PKG_FOUND TRUE)\n";
        s += "  endif()\n";
        s += "endif()\n\n";

        s += "if (NOT VIX_PKG_FOUND)\n";
        s += "  message(FATAL_ERROR \"Could not find Vix/vix package config\")\n";
        s += "endif()\n\n";

        s += "# Choisir la meilleure cible à lier :\n";
        s += "#   1) vix::core (idéal)\n";
        s += "#   2) Vix::core\n";
        s += "#   3) vix::vix / Vix::vix (fallback)\n";
        s += "set(VIX_CORE_TARGET \"\")\n\n";

        s += "if (TARGET vix::core)\n";
        s += "  set(VIX_CORE_TARGET vix::core)\n";
        s += "elseif (TARGET Vix::core)\n";
        s += "  set(VIX_CORE_TARGET Vix::core)\n";
        s += "elseif (TARGET vix::vix)\n";
        s += "  set(VIX_CORE_TARGET vix::vix)\n";
        s += "elseif (TARGET Vix::vix)\n";
        s += "  set(VIX_CORE_TARGET Vix::vix)\n";
        s += "else()\n";
        s += "  message(FATAL_ERROR \"No Vix core/main target found (expected vix::core, Vix::core, vix::vix or Vix::vix)\")\n";
        s += "endif()\n\n";

        // ===== Executable + liens =====
        s += "add_executable(" + exeName + " \"" + cppPath.string() + "\")\n\n";
        s += "target_link_libraries(" + exeName + " PRIVATE\n";
        s += "  ${VIX_CORE_TARGET}\n";
        s += "  Boost::system\n";
        s += "  Boost::thread\n";
        s += "  Boost::filesystem\n";
        s += "  fmt::fmt\n";
        s += "  OpenSSL::SSL\n";
        s += "  OpenSSL::Crypto\n";
        s += ")\n\n";

        // Target run
        s += "add_custom_target(run\n";
        s += "  COMMAND $<TARGET_FILE:" + exeName + ">\n";
        s += "  DEPENDS " + exeName + "\n";
        s += "  USES_TERMINAL\n";
        s += ")\n";

        return s;
    }

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

} // namespace vix::commands::RunCommand::detail
