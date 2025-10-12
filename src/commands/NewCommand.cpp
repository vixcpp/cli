#include <vix/cli/commands/NewCommand.hpp>
#include <vix/utils/Logger.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>
#include <cstdlib>
#include <vector>
#include <optional>

#include <random>
#include <system_error>

namespace fs = std::filesystem;

namespace
{

    // --------- Minimal hello-world server (API Vix moderne) ----------
    constexpr const char *kMainCpp = R"(#include <vix.hpp>
using namespace Vix;

int main()
{
    App app;

    // GET /
    app.get("/", [](auto&, auto& res) {
        res.json({"message", "Hello world"});
    });

    app.run(8080);
}
)";

    // README generator (modern, cross-platform, rich content)
    std::string make_readme(const std::string &projectName)
    {
        std::string readme;

        readme += "# 🧩 " + projectName + " — Example project using [Vix.cpp](https://github.com/vixcpp/vix)\n\n";
        readme += projectName + " is a minimal example showcasing how to build and run a C++ web application using the **Vix.cpp** framework.\n";
        readme += "It demonstrates a clean, cross-platform setup with `CMakePresets.json` and an optional `Makefile` for quick builds.\n\n";
        readme += "---\n\n";

        readme += "## 🚀 Features\n\n";
        readme += "- Simple **HTTP server** powered by `Vix::App`\n";
        readme += "- Cross-platform build system (Linux / macOS / Windows)\n";
        readme += "- Modern **C++20** codebase\n";
        readme += "- Configurable via CMake presets (`dev-ninja`, `dev-msvc`)\n";
        readme += "- Optional sanitizers for debug builds\n";
        readme += "- Integrated logging (via Vix logger)\n\n";
        readme += "---\n\n";

        readme += "## 🏗️ Project Structure\n\n";
        readme += "```\n";
        readme += projectName + "/\n";
        readme += "├── CMakeLists.txt        # Main build configuration\n";
        readme += "├── CMakePresets.json     # Cross-platform presets\n";
        readme += "├── Makefile              # Simplified build helper\n";
        readme += "├── README.md             # Project documentation\n";
        readme += "└── src/\n";
        readme += "    └── main.cpp          # Application entry point\n";
        readme += "```\n\n";
        readme += "---\n\n";

        readme += "## ⚙️ Requirements\n\n";
        readme += "- **CMake ≥ 3.20**\n";
        readme += "- **C++20 compiler**\n";
        readme += "  - Linux/macOS: Clang ≥ 15 or GCC ≥ 11\n";
        readme += "  - Windows: Visual Studio 2022 (MSVC 19.3+)\n";
        readme += "- **Ninja** (optional, for fast builds)\n";
        readme += "- **Vix.cpp installed** under `/usr/local` or built locally\n\n";
        readme += "---\n\n";

        readme += "## 🔧 Build and Run\n\n";
        readme += "### 🐧 Linux / macOS\n\n";
        readme += "```bash\n";
        readme += "make build      # Configure + build (via preset)\n";
        readme += "make run        # Build and execute\n";
        readme += "```\n\n";

        readme += "### or manually with CMake:\n\n";
        readme += "```bash\n";
        readme += "cmake --preset dev-ninja\n";
        readme += "cmake --build --preset dev-ninja\n";
        readme += "```\n\n";

        readme += "### 🪟 Windows (Visual Studio 2022)\n\n";
        readme += "```powershell\n";
        readme += "cmake --preset dev-msvc\n";
        readme += "cmake --build --preset dev-msvc\n";
        readme += "```\n\n";
        readme += "> The `run` target is already defined in the CMake file — it will execute the compiled binary automatically.\n\n";
        readme += "---\n\n";

        readme += "## 🧰 Useful Commands\n\n";
        readme += "| Command                      | Description                             |\n";
        readme += "| ---------------------------- | --------------------------------------- |\n";
        readme += "| `make build`                 | Configure and build with default preset |\n";
        readme += "| `make run`                   | Build and run the app                   |\n";
        readme += "| `make clean`                 | Remove all build folders                |\n";
        readme += "| `make rebuild`               | Clean and rebuild everything            |\n";
        readme += "| `make PRESET=dev-msvc build` | Build with custom preset (e.g. Windows) |\n\n";
        readme += "---\n\n";

        readme += "## ⚡ Example Output\n\n";
        readme += "When built successfully, you’ll see logs like:\n\n";
        readme += "```bash\n";
        readme += "[2025-10-12 13:41:23.220] [vixLogger] [info] Using configuration file: /home/user/vixcpp/vix/config/config.json\n";
        readme += "[2025-10-12 13:41:23.221] [vixLogger] [info] Acceptor initialized on port 8080\n";
        readme += "[2025-10-12 13:41:23.221] [vixLogger] [info] Server request timeout set to 5000 ms\n";
        readme += "```\n\n";
        readme += "Visit **http://localhost:8080/** to test.\n\n";
        readme += "---\n\n";

        readme += "## 🧩 About Vix.cpp\n\n";
        readme += "[Vix.cpp](https://github.com/vixcpp/vix) is a high-performance, modular C++ web framework inspired by **FastAPI**, **Express.js**, and **Vue.js**.\n\n";
        readme += "It offers:\n\n";
        readme += "- Extreme performance (**40k+ requests/sec**)\n";
        readme += "- Clean syntax (`App app; app.get(\"/\", ...);`)\n";
        readme += "- Modular architecture (`core`, `orm`, `cli`, `json`, `utils`, etc.)\n";
        readme += "- Simple CMake integration for external apps\n\n";
        readme += "---\n\n";

        readme += "## 🪪 License\n\n";
        readme += "MIT © [Vix.cpp Authors](https://github.com/vixcpp)\n";

        return readme;
    }

    // ------- CMakeLists template with package-first + fallback (portable) ------
    static std::string make_cmakelists(const std::string &projectName)
    {
        std::string s;
        s.reserve(16384);

        s += "cmake_minimum_required(VERSION 3.20)\n";
        s += "project(" + projectName + " LANGUAGES CXX)\n\n";

        s += "# ===== C++ standard =====\n";
        s += "set(CMAKE_CXX_STANDARD 20)\n";
        s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

        s += "option(VIX_ENABLE_SANITIZERS \"Enable ASan/UBSan (dev only)\" OFF)\n";
        s += "option(VIX_USE_INSTALLED \"Prefer installed Vix packages if available\" ON)\n\n";

        s += "# ===== Search prefixes (edit if your install lives elsewhere) =====\n";
        s += "list(APPEND CMAKE_PREFIX_PATH\n";
        s += "  \"/usr/local\"\n";
        s += "  \"/usr/local/lib/cmake/Vix\"\n";
        s += "  \"/usr/local/lib/cmake/VixOrm\"\n";
        s += ")\n\n";

        s += "# -----------------------------------------------------------------------------\n";
        s += "# 1) Try installed packages (and pull their deps); otherwise, fall back\n";
        s += "# -----------------------------------------------------------------------------\n";
        s += "set(_VIX_INC \"/usr/local/include\")\n";
        s += "set(_VIX_LIBDIR \"/usr/local/lib\")\n\n";

        s += "set(_use_fallbacks OFF)\n";
        s += "if (VIX_USE_INSTALLED)\n";
        s += "  # Do not pull ORM in simple apps\n";
        s += "  set(CMAKE_DISABLE_FIND_PACKAGE_VixOrm ON)\n\n";
        s += "  find_package(Vix CONFIG QUIET)\n\n";
        s += "  if (TARGET Vix::vix)\n";
        s += "    # Ensure transitive deps exported by Vix exist (create shims if missing)\n";
        s += "    find_package(spdlog CONFIG QUIET)\n";
        s += "    if (NOT TARGET spdlog::spdlog_header_only)\n";
        s += "      add_library(spdlog::spdlog_header_only INTERFACE IMPORTED)\n";
        s += "      target_include_directories(spdlog::spdlog_header_only INTERFACE \"/usr/local/include\" \"/usr/include\")\n";
        s += "      message(STATUS \"Shim: created spdlog::spdlog_header_only (headers only)\")\n";
        s += "    endif()\n\n";
        s += "    find_package(nlohmann_json CONFIG QUIET)\n";
        s += "    if (NOT TARGET nlohmann_json::nlohmann_json)\n";
        s += "      add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)\n";
        s += "      target_include_directories(nlohmann_json::nlohmann_json INTERFACE \"/usr/local/include\" \"/usr/include\")\n";
        s += "      message(STATUS \"Shim: created nlohmann_json::nlohmann_json (headers only)\")\n";
        s += "    endif()\n\n";
        s += "    find_package(Boost COMPONENTS filesystem QUIET)\n";
        s += "    if (NOT TARGET Boost::filesystem)\n";
        s += "      if (Boost_FOUND AND Boost_FILESYSTEM_LIBRARY)\n";
        s += "        add_library(Boost::filesystem UNKNOWN IMPORTED)\n";
        s += "        set_target_properties(Boost::filesystem PROPERTIES\n";
        s += "          IMPORTED_LOCATION \"${Boost_FILESYSTEM_LIBRARY}\"\n";
        s += "          INTERFACE_INCLUDE_DIRECTORIES \"${Boost_INCLUDE_DIRS}\")\n";
        s += "        message(STATUS \"Using Boost::filesystem from ${Boost_FILESYSTEM_LIBRARY}\")\n";
        s += "      else()\n";
        s += "        message(WARNING \"Installed Vix requires Boost::filesystem but it was not found; falling back to local Vix targets.\")\n";
        s += "        set(_use_fallbacks ON)\n";
        s += "      endif()\n";
        s += "    endif()\n";
        s += "  else()\n";
        s += "    set(_use_fallbacks ON)\n";
        s += "  endif()\n";
        s += "else()\n";
        s += "  set(_use_fallbacks ON)\n";
        s += "endif()\n\n";

        s += "# -----------------------------------------------------------------------------\n";
        s += "# 2) Fallbacks: build IMPORTED targets core/utils/json and umbrella Vix::vix\n";
        s += "# -----------------------------------------------------------------------------\n";
        s += "if (_use_fallbacks)\n";
        s += "  message(WARNING \"Using local Vix fallbacks (core/utils/json + umbrella target)\")\n\n";
        s += "  if (NOT TARGET Vix::core)\n";
        s += "    find_library(VIX_CORE_LIB NAMES vix_core PATHS \"${_VIX_LIBDIR}\" NO_DEFAULT_PATH)\n";
        s += "    if (VIX_CORE_LIB)\n";
        s += "      add_library(Vix::core STATIC IMPORTED GLOBAL)\n";
        s += "      set_target_properties(Vix::core PROPERTIES\n";
        s += "        IMPORTED_LOCATION \"${VIX_CORE_LIB}\"\n";
        s += "        INTERFACE_INCLUDE_DIRECTORIES \"${_VIX_INC}\")\n";
        s += "      message(STATUS \"Fallback Vix::core at ${VIX_CORE_LIB}\")\n";
        s += "    endif()\n";
        s += "  endif()\n\n";
        s += "  if (NOT TARGET Vix::utils)\n";
        s += "    find_library(VIX_UTILS_LIB NAMES vix_utils PATHS \"${_VIX_LIBDIR}\" NO_DEFAULT_PATH)\n";
        s += "    if (VIX_UTILS_LIB)\n";
        s += "      add_library(Vix::utils STATIC IMPORTED GLOBAL)\n";
        s += "      set_target_properties(Vix::utils PROPERTIES\n";
        s += "        IMPORTED_LOCATION \"${VIX_UTILS_LIB}\"\n";
        s += "        INTERFACE_INCLUDE_DIRECTORIES \"${_VIX_INC}\")\n";
        s += "      message(STATUS \"Fallback Vix::utils at ${VIX_UTILS_LIB}\")\n";
        s += "    endif()\n";
        s += "  endif()\n\n";
        s += "  if (NOT TARGET Vix::json AND EXISTS \"${_VIX_INC}/vix/json/json.hpp\")\n";
        s += "    add_library(Vix::json INTERFACE IMPORTED GLOBAL)\n";
        s += "    set_target_properties(Vix::json PROPERTIES\n";
        s += "      INTERFACE_INCLUDE_DIRECTORIES \"${_VIX_INC}\")\n";
        s += "    message(STATUS \"Fallback Vix::json headers at ${_VIX_INC}\")\n";
        s += "  endif()\n\n";
        s += "  if (NOT TARGET Vix::vix)\n";
        s += "    set(_JSON_TGT \"\")\n";
        s += "    if (TARGET Vix::json)\n";
        s += "      set(_JSON_TGT \"Vix::json\")\n";
        s += "    endif()\n";
        s += "    if (TARGET Vix::core AND TARGET Vix::utils AND NOT _JSON_TGT STREQUAL \"\")\n";
        s += "      add_library(Vix::vix INTERFACE IMPORTED GLOBAL)\n";
        s += "      set_property(TARGET Vix::vix PROPERTY INTERFACE_LINK_LIBRARIES Vix::core;Vix::utils;${_JSON_TGT})\n";
        s += "      set_property(TARGET Vix::vix PROPERTY INTERFACE_INCLUDE_DIRECTORIES \"${_VIX_INC}\")\n";
        s += "      message(STATUS \"Created umbrella Vix::vix from fallbacks\")\n";
        s += "    else()\n";
        s += "      message(FATAL_ERROR \"Fallbacks incomplete: need core+utils+json\")\n";
        s += "    endif()\n";
        s += "  endif()\n";
        s += "endif()\n\n";

        s += "# -----------------------------------------------------------------------------\n";
        s += "# 3) App target\n";
        s += "# -----------------------------------------------------------------------------\n";
        s += "add_executable(" + projectName + " src/main.cpp)\n";
        s += "add_custom_target(run\n";
        s += "  COMMAND $<TARGET_FILE:" + projectName + ">\n";
        s += "  DEPENDS " + projectName + "\n";
        s += "  USES_TERMINAL\n";
        s += "  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}\n";
        s += ")\n";
        s += "set_target_properties(" + projectName + " PROPERTIES\n";
        s += "  RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}\"\n";
        s += "  INSTALL_RPATH_USE_LINK_PATH ON\n";
        s += ")\n";
        s += "target_link_libraries(" + projectName + " PRIVATE Vix::vix)\n\n";

        s += "# System deps\n";
        s += "find_package(Threads REQUIRED)\n";
        s += "target_link_libraries(" + projectName + " PRIVATE Threads::Threads)\n\n";

        s += "find_package(OpenSSL QUIET)\n";
        s += "if (OpenSSL_FOUND)\n";
        s += "  target_link_libraries(" + projectName + " PRIVATE OpenSSL::SSL OpenSSL::Crypto)\n";
        s += "endif()\n\n";

        s += "if (UNIX AND NOT APPLE)\n";
        s += "  target_link_libraries(" + projectName + " PRIVATE dl)\n";
        s += "endif()\n\n";

        s += "# Warnings\n";
        s += "if (MSVC)\n";
        s += "  target_compile_options(" + projectName + " PRIVATE /W4 /permissive-)\n";
        s += "else()\n";
        s += "  target_compile_options(" + projectName + " PRIVATE -Wall -Wextra -Wpedantic)\n";
        s += "endif()\n\n";

        s += "# Sanitizers (optional)\n";
        s += "if (VIX_ENABLE_SANITIZERS AND NOT MSVC)\n";
        s += "  target_compile_options(" + projectName + " PRIVATE -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)\n";
        s += "  target_link_options(" + projectName + " PRIVATE -fsanitize=address,undefined)\n";
        s += "endif()\n";

        return s;
    }

    // Makefile generator (cross-platform helper for Vix apps)
    std::string make_makefile(const std::string &projectName)
    {
        (void)projectName; // not used, but kept for symmetry
        return R"(# =============================================================
# Vix App — Cross-platform build helper
# =============================================================
# Usage:
#   make build     → configure + build
#   make run       → build + execute
#   make clean     → delete build folder
#   make rebuild   → full rebuild
# =============================================================

PRESET ?= dev-ninja
CMAKE = cmake

all: build

# Configure + build (uses preset)
build:
	@$(CMAKE) --preset $(PRESET)
	@$(CMAKE) --build --preset $(PRESET)

# Run target defined in CMakeLists (cross-platform)
run:
	@$(CMAKE) --build --preset $(PRESET)

# Clean all builds
clean:
	rm -rf build-* CMakeFiles CMakeCache.txt

# Force a full rebuild
rebuild: clean build
)";
    }

    // CMakePresets.json generator (cross-platform presets: Ninja + MSVC)
    std::string make_cmake_presets_json()
    {
        // NOTE:
        // - Raw string with a custom delimiter (JSON) to avoid accidental )" collisions.
        // - Removed unnecessary "strategy" in the MSVC architecture block (simpler, widely supported).
        return R"JSON({
  "version": 6,
  "configurePresets": [
    {
      "name": "dev-ninja",
      "displayName": "Dev (Ninja, Release)",
      "generator": "Ninja",
      "binaryDir": "build-ninja",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "VIX_USE_INSTALLED": "OFF"
      }
    },
    {
      "name": "dev-msvc",
      "displayName": "Dev (MSVC, Release)",
      "generator": "Visual Studio 17 2022",
      "architecture": { "value": "x64" },
      "binaryDir": "build-msvc",
      "cacheVariables": {
        "VIX_USE_INSTALLED": "OFF"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "dev-ninja",
      "displayName": "Build+Run (Ninja)",
      "configurePreset": "dev-ninja",
      "targets": ["run"]
    },
    {
      "name": "dev-msvc",
      "displayName": "Build+Run (MSVC)",
      "configurePreset": "dev-msvc",
      "configuration": "Release",
      "targets": ["run"]
    }
  ]
}
)JSON";
    }

    // Écrit un fichier texte de manière sûre : fichier temporaire puis renommage.
    // - Crée les répertoires parents si nécessaires.
    // - Utilise un fichier .tmp dans le même dossier pour éviter les problèmes de volume.
    // - Tente un remplacement atomique (selon l’OS / FS).
    inline void write_text_file(const fs::path &p, std::string_view content)
    {
        std::error_code ec;

        // 1) Assure l'existence du parent (si applicable)
        const fs::path parent = p.parent_path();
        if (!parent.empty())
        {
            fs::create_directories(parent, ec);
            if (ec)
            {
                throw std::runtime_error("Cannot create directories for: " + parent.string() +
                                         " — " + ec.message());
            }
        }

        // 2) Fichier temporaire à côté de la cible (avec quelques tentatives)
        auto make_tmp_name = [&]()
        {
            std::mt19937_64 rng{std::random_device{}()};
            auto rnd = rng();
            return p.string() + ".tmp-" + std::to_string(rnd);
        };

        fs::path tmp;
        for (int tries = 0; tries < 3; ++tries)
        {
            tmp = make_tmp_name();
            if (!fs::exists(tmp, ec))
                break;
            if (tries == 2)
            {
                throw std::runtime_error("Cannot generate unique temp file near: " + p.string());
            }
        }

        // 3) Écriture (binaire) + flush + close
        {
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs)
            {
                fs::remove(tmp, ec); // best-effort cleanup
                throw std::runtime_error("Cannot open temp file for write: " + tmp.string());
            }

            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!ofs)
            {
                ofs.close();
                fs::remove(tmp, ec);
                throw std::runtime_error("Failed to write file: " + tmp.string());
            }

            ofs.flush();
            if (!ofs)
            {
                ofs.close();
                fs::remove(tmp, ec);
                throw std::runtime_error("Failed to flush file: " + tmp.string());
            }
            // close via dtor
        }

        // 4) Rename → p (tentative atomique). Sous Windows, si p existe, rename peut échouer.
        fs::rename(tmp, p, ec);
        if (ec)
        {
            // Essaye de supprimer la cible existante puis renommer à nouveau
            fs::remove(p, ec); // ignorer l'erreur (si absent / verrouillé)
            ec.clear();
            fs::rename(tmp, p, ec);
            if (ec)
            {
                std::error_code ec2;
                fs::remove(tmp, ec2); // éviter les orphelins
                throw std::runtime_error("Failed to move temp file to destination: " +
                                         tmp.string() + " → " + p.string() + " — " + ec.message());
            }
        }
    }

    // Renvoie true si le répertoire est vide ou n'existe pas; false sinon.
    // N'émet **pas** d'exception : utile dans les checks préalables.
    inline bool is_dir_empty(const fs::path &p) noexcept
    {
        std::error_code ec;

        if (!fs::exists(p, ec))
            return true; // inexistant = considéré vide
        if (ec)
            return false;
        if (!fs::is_directory(p, ec))
            return false;
        if (ec)
            return false;

        fs::directory_iterator it(p, ec);
        if (ec)
            return false;
        return (it == fs::directory_iterator{});
    }

    // Récupère la valeur de --dir / -d si présente.
    // Supporte: "-d PATH", "--dir PATH", et "--dir=PATH".
    // Évite de prendre une autre option comme valeur (ex: "-d --flag").
    inline std::optional<std::string> pick_dir_opt(const std::vector<std::string> &args)
    {
        auto is_option = [](std::string_view sv)
        {
            return !sv.empty() && sv.front() == '-';
        };

        for (size_t i = 0; i < args.size(); ++i)
        {
            const std::string &a = args[i];

            if (a == "-d" || a == "--dir")
            {
                if (i + 1 < args.size() && !is_option(args[i + 1]))
                {
                    return args[i + 1];
                }
                // option sans valeur → ignorée (caller gère le défaut)
                return std::nullopt;
            }

            // format --dir=/chemin
            constexpr const char prefix[] = "--dir=";
            if (a.rfind(prefix, 0) == 0)
            {
                std::string val = a.substr(sizeof(prefix) - 1);
                // autoriser --dir="" (revient à std::nullopt)
                if (val.empty())
                    return std::nullopt;
                return val;
            }
        }
        return std::nullopt;
    }

} // namespace

namespace Vix::Commands::NewCommand
{
    int run(const std::vector<std::string> &args)
    {
        auto &logger = ::Vix::Logger::getInstance();

        if (args.empty())
        {
            logger.logModule("NewCommand", ::Vix::Logger::Level::ERROR,
                             "Usage: vix new <name|path> [-d|--dir <base_dir>]");
            return 1;
        }

        const std::string nameOrPath = args[0];
        std::optional<std::string> baseOpt = pick_dir_opt(args);

        try
        {
            fs::path dest;

            // Si -d/--dir est fourni → on résout base + nom (en respectant absolu/relatif)
            if (baseOpt.has_value())
            {
                fs::path base = fs::path(*baseOpt);
                if (!fs::exists(base) || !fs::is_directory(base))
                {
                    logger.logModule("NewCommand", ::Vix::Logger::Level::ERROR,
                                     "Base directory '{}' is invalid.", base.string());
                    return 2;
                }
                fs::path np = fs::path(nameOrPath);
                // base est existant → canonical OK
                dest = np.is_absolute() ? np : (fs::canonical(base) / np);
            }
            else
            {
                // Pas de base explicite : on respecte absolu/relatif tel que fourni par l'utilisateur
                fs::path np = fs::path(nameOrPath);
                dest = np.is_absolute() ? np : (fs::current_path() / np);
            }

            const fs::path projectDir = dest;
            const fs::path srcDir = projectDir / "src";
            const fs::path mainCpp = srcDir / "main.cpp";
            const fs::path cmakeLists = projectDir / "CMakeLists.txt";
            const fs::path readmeFile = projectDir / "README.md";
            const fs::path presetsFile = projectDir / "CMakePresets.json";
            const fs::path makefilePath = projectDir / "Makefile";

            // Sécurité : ne pas écraser un dossier NON vide
            if (fs::exists(projectDir) && !is_dir_empty(projectDir))
            {
                logger.logModule("NewCommand", ::Vix::Logger::Level::ERROR,
                                 "Directory '{}' already exists and is not empty.", projectDir.string());
                return 3;
            }

            // Arborescence + fichiers
            fs::create_directories(srcDir);
            write_text_file(mainCpp, kMainCpp);
            write_text_file(cmakeLists, make_cmakelists(projectDir.filename().string()));
            write_text_file(readmeFile, make_readme(projectDir.filename().string()));
            write_text_file(presetsFile, make_cmake_presets_json());
            write_text_file(makefilePath, make_makefile(projectDir.filename().string()));

            logger.logModule("NewCommand", ::Vix::Logger::Level::INFO,
                             "✅ Project '{}' created at {}", projectDir.filename().string(), projectDir.string());

            // Conseils multiplateforme (presets + Makefile)
            logger.logModule("NewCommand", ::Vix::Logger::Level::INFO,
                             "Build & Run (Linux/macOS with Ninja):\n"
                             "  cd \"{0}\"\n"
                             "  cmake --preset dev-ninja\n"
                             "  cmake --build --preset dev-ninja\n"
                             "\n"
                             "Build & Run (Windows, Visual Studio 2022):\n"
                             "  cd \"{0}\"\n"
                             "  cmake --preset dev-msvc\n"
                             "  cmake --build --preset dev-msvc\n"
                             "\n"
                             "Or using Makefile helpers (default preset=dev-ninja):\n"
                             "  cd \"{0}\"\n"
                             "  make build\n"
                             "  make run",
                             projectDir.string());

            return 0;
        }
        catch (const std::exception &ex)
        {
            logger.logModule("NewCommand", ::Vix::Logger::Level::ERROR,
                             "Failed to create project: {}", ex.what());
            return 4;
        }
    }
} // namespace Vix::Commands::NewCommand
