#include <vix/cli/commands/NewCommand.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/Utils.hpp>

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
using namespace vix::cli::style;
using namespace vix::cli::util;

namespace
{
  constexpr const char *kMainCpp = R"(#include <vix.hpp>
using namespace vix;

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

  constexpr const char *kBasicTestCpp = R"(#include <iostream>

int main()
{
    std::cout << "basic test OK\n";
    return 0;
}
)";

  std::string make_readme(const std::string &projectName)
  {
    std::string readme;
    readme.reserve(22000);

    readme += "# " + projectName + " — Example project using [Vix.cpp](https://github.com/vixcpp/vix)\n\n";
    readme += projectName + " is a minimal example showing how to **build**, **run**, **test**, and **hot-reload** a C++ app with **Vix.cpp**.\n";
    readme += "It uses `CMakePresets.json` for a clean cross-platform workflow, supports optional **Vix ORM**, and includes a basic **CTest** test out of the box.\n\n";
    readme += "---\n\n";

    // Features
    readme += "## Features\n\n";
    readme += "- Simple **HTTP server** powered by `vix::App`\n";
    readme += "- Modern **C++20** codebase\n";
    readme += "- Cross-platform build workflow via **CMake presets**\n";
    readme += "- **Hot reload** dev mode: rebuild & restart on save (`vix dev`)\n";
    readme += "- Built-in **tests** (CTest) via `vix tests`\n";
    readme += "- Optional sanitizers (`--san`, `--ubsan`)\n";
    readme += "- Optional **ORM** (`VIX_USE_ORM=ON`)\n\n";
    readme += "---\n\n";

    // Project structure
    readme += "## Project Structure\n\n";
    readme += "```\n";
    readme += projectName + "/\n";
    readme += "├── CMakeLists.txt\n";
    readme += "├── CMakePresets.json\n";
    readme += "├── README.md\n";
    readme += "├── src/\n";
    readme += "│   └── main.cpp\n";
    readme += "└── tests/\n";
    readme += "    └── test_basic.cpp\n";
    readme += "```\n\n";
    readme += "---\n\n";

    // Requirements
    readme += "## Requirements\n\n";
    readme += "- **CMake ≥ 3.20**\n";
    readme += "- **C++20 compiler**\n";
    readme += "  - Linux/macOS: Clang ≥ 15 or GCC ≥ 11\n";
    readme += "  - Windows: Visual Studio 2022 (MSVC 19.3+)\n";
    readme += "- **Vix.cpp installed** (system install or local build)\n\n";
    readme += "---\n\n";

    // Quick start
    readme += "## Quick start\n\n";
    readme += "```bash\n";
    readme += "vix build\n";
    readme += "vix run\n";
    readme += "```\n\n";
    readme += "Open **http://localhost:8080/** in your browser.\n\n";
    readme += "---\n\n";

    // Vix CLI workflow
    readme += "## Vix CLI workflow\n\n";
    readme += "```bash\n";
    readme += "vix build                 # Configure & build (presets)\n";
    readme += "vix run                   # Run (builds if needed)\n";
    readme += "vix dev                   # Hot reload: watch + rebuild + restart\n";
    readme += "vix check                 # Validate project build\n";
    readme += "vix tests                 # Run tests (alias of `vix check --tests`)\n";
    readme += "```\n\n";

    // Tests (vix tests)
    readme += "## Tests\n\n";
    readme += "This project uses **CTest**. Vix provides a dedicated command:\n\n";
    readme += "```bash\n";
    readme += "vix tests                 # Run all tests\n";
    readme += "vix tests --list          # List tests (ctest --show-only)\n";
    readme += "vix tests --fail-fast     # Stop at first failure (ctest --stop-on-failure)\n";
    readme += "```\n\n";

    readme += "### Passing extra CTest arguments\n\n";
    readme += "`vix tests` forwards extra arguments to `ctest` via `--ctest-arg`:\n\n";
    readme += "```bash\n";
    readme += "vix tests --ctest-arg -V                    # verbose output\n";
    readme += "vix tests --ctest-arg -R --ctest-arg \"basic\" # regex filter\n";
    readme += "vix tests --ctest-arg -j --ctest-arg 8      # run tests in parallel\n";
    readme += "```\n\n";

    readme += "> Tip: if you use `--list`, Vix will not add extra `--output-on-failure` flags to keep output clean.\n\n";
    readme += "---\n\n";

    // ORM
    readme += "## Enable ORM (optional)\n\n";
    readme += "If your Vix installation exports `vix::orm`, you can enable it like this:\n\n";
    readme += "```bash\n";
    readme += "vix build -D VIX_USE_ORM=ON\n";
    readme += "vix run   -D VIX_USE_ORM=ON\n";
    readme += "vix dev   -D VIX_USE_ORM=ON\n";
    readme += "```\n\n";
    readme += "> If `vix::orm` is not available in your install, CMake will fail with a clear error.\n\n";
    readme += "---\n\n";

    // Sanitizers
    readme += "## Sanitizers (optional)\n\n";
    readme += "Vix supports sanitizers via CLI flags and presets:\n\n";
    readme += "```bash\n";
    readme += "vix run  --san            # ASan+UBSan (preset-aware)\n";
    readme += "vix run  --ubsan          # UBSan-only\n";
    readme += "vix tests --san           # build/run tests with sanitizers\n";
    readme += "vix tests --ubsan         # UBSan-only tests\n";
    readme += "```\n\n";

    readme += "You can also enable sanitizers manually using CMake definitions:\n\n";
    readme += "```bash\n";
    readme += "vix build -D VIX_ENABLE_SANITIZERS=ON -D VIX_SANITIZER_MODE=asan_ubsan\n";
    readme += "vix build -D VIX_ENABLE_SANITIZERS=ON -D VIX_SANITIZER_MODE=ubsan\n";
    readme += "```\n\n";

    readme += "### Notes\n\n";
    readme += "- Sanitizers are intended for **dev/debug** builds.\n";
    readme += "- The generated CMake applies sanitizer flags to the **app target** and the **test targets**.\n\n";
    readme += "---\n\n";

    // Manual CMake
    readme += "## Manual CMake (optional)\n\n";
    readme += "If you prefer not using `vix`, you can still build and test using plain CMake:\n\n";
    readme += "```bash\n";
    readme += "cmake --preset dev-ninja\n";
    readme += "cmake --build --preset dev-ninja\n";
    readme += "ctest --test-dir build-ninja --output-on-failure\n";
    readme += "```\n\n";

    readme += "### Windows (Visual Studio 2022)\n\n";
    readme += "```powershell\n";
    readme += "cmake --preset dev-msvc\n";
    readme += "cmake --build --preset dev-msvc\n";
    readme += "ctest --test-dir build-msvc --output-on-failure\n";
    readme += "```\n\n";
    readme += "---\n\n";

    // Pack + Verify
    readme += "## Packaging & security (optional)\n\n";
    readme += "Vix provides packaging and artifact verification:\n\n";
    readme += "```bash\n";
    readme += "vix pack --name " + projectName + " --version 1.0.0\n";
    readme += "vix verify --require-signature\n";
    readme += "```\n\n";

    readme += "Environment variables:\n\n";
    readme += "- `VIX_MINISIGN_SECKEY=path` — secret key used by `vix pack` to sign `payload.digest`\n";
    readme += "- `VIX_MINISIGN_PUBKEY=path` — public key used by `vix verify` (if `--pubkey` not provided)\n\n";
    readme += "---\n\n";

    // Useful commands table
    readme += "## Useful Commands\n\n";
    readme += "| Command | Description |\n";
    readme += "|--------|-------------|\n";
    readme += "| `vix build` | Configure + build the project |\n";
    readme += "| `vix run` | Run the project (builds if needed) |\n";
    readme += "| `vix dev` | Hot reload (watch + rebuild + restart) |\n";
    readme += "| `vix check` | Validate a project build |\n";
    readme += "| `vix tests` | Run tests (alias of `vix check --tests`) |\n";
    readme += "| `vix tests --list` | List available tests |\n";
    readme += "| `vix tests --fail-fast` | Stop at first failure |\n";
    readme += "| `vix pack` | Create `dist/<name>@<version>` (+ optional `.vixpkg`) |\n";
    readme += "| `vix verify` | Verify `dist/<name>@<version>` or a `.vixpkg` artifact |\n";
    readme += "| `vix help` | Show CLI help |\n\n";
    readme += "---\n\n";

    // Example output
    readme += "## Example Output\n\n";
    readme += "When the server starts successfully, you’ll see logs like:\n\n";
    readme += "```bash\n";
    readme += "[I] Config loaded from .../config.json\n";
    readme += "[I] [ThreadPool] started with threads=...\n";
    readme += "[I] Acceptor initialized on port 8080\n";
    readme += "[I] Server request timeout set to 5000 ms\n";
    readme += "```\n\n";

    readme += "Running tests:\n\n";
    readme += "```bash\n";
    readme += "vix tests\n";
    readme += "```\n\n";
    readme += "---\n\n";

    // About Vix.cpp
    readme += "## About Vix.cpp\n\n";
    readme += "[Vix.cpp](https://github.com/vixcpp/vix) is a high-performance, modular C++ backend runtime inspired by **FastAPI**, **Express.js**, and modern tooling.\n\n";
    readme += "- Clean routing (`app.get(\"/\", ...)`)\n";
    readme += "- Modular architecture (`core`, `utils`, `json`, `websocket`, `orm`, ...)\n";
    readme += "- Simple CMake integration for external apps\n\n";
    readme += "---\n\n";

    // License
    readme += "## License\n\n";
    readme += "MIT © [Vix.cpp Authors](https://github.com/vixcpp)\n";

    return readme;
  }

  static std::string make_cmakelists(const std::string &projectName)
  {
    std::string s;
    s.reserve(16000);

    s += "cmake_minimum_required(VERSION 3.20)\n";
    s += "project(" + projectName + " LANGUAGES CXX)\n\n";

    s += "# ======================================================\n";
    s += "# Generated by Vix CLI — Application CMakeLists.txt\n";
    s += "# Vix core by default; ORM optional.\n";
    s += "# ======================================================\n\n";

    s += "set(CMAKE_CXX_STANDARD 20)\n";
    s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

    s += "option(VIX_ENABLE_SANITIZERS \"Enable ASan/UBSan (dev only)\" OFF)\n";
    s += "option(VIX_USE_ORM \"Enable Vix ORM (requires vix::orm in install)\" OFF)\n";
    s += "option(VIX_LINK_STATIC \"Static libstdc++/libgcc (glibc-safe)\" OFF)\n";
    s += "option(VIX_LINK_FULL_STATIC \"Full static link (-static). Prefer musl.\" OFF)\n";
    s += "set(VIX_SANITIZER_MODE \"asan_ubsan\" CACHE STRING \"Sanitizer mode: asan_ubsan | ubsan\")\n\n";

    s += "# Prefer lowercase package, fallback to legacy Vix\n";
    s += "find_package(vix QUIET CONFIG)\n";
    s += "if (NOT vix_FOUND)\n";
    s += "  find_package(Vix CONFIG REQUIRED)\n";
    s += "endif()\n\n";

    // Main target
    s += "add_executable(" + projectName + " src/main.cpp)\n\n";
    s += "target_link_libraries(" + projectName + " PRIVATE vix::vix)\n\n";

    // Optional ORM
    s += "if (VIX_USE_ORM)\n";
    s += "  if (TARGET vix::orm)\n";
    s += "    target_link_libraries(" + projectName + " PRIVATE vix::orm)\n";
    s += "    target_compile_definitions(" + projectName + " PRIVATE VIX_USE_ORM=1)\n";
    s += "  else()\n";
    s += "    message(FATAL_ERROR \"VIX_USE_ORM=ON but vix::orm target is not available in this Vix install\")\n";
    s += "  endif()\n";
    s += "endif()\n\n";

    // Warnings
    s += "if (MSVC)\n";
    s += "  target_compile_options(" + projectName + " PRIVATE /W4 /permissive-)\n";
    s += "else()\n";
    s += "  target_compile_options(" + projectName + " PRIVATE -Wall -Wextra -Wpedantic)\n";
    s += "endif()\n\n";

    // Static linking policy
    s += "# ------------------------------------------------------\n";
    s += "# Static linking (explicit)\n";
    s += "# - VIX_LINK_STATIC: glibc-safe static C++ runtime only\n";
    s += "# - VIX_LINK_FULL_STATIC: full -static (prefer musl toolchain)\n";
    s += "# ------------------------------------------------------\n";
    s += "function(vix_apply_static_link_flags tgt)\n";
    s += "  if (MSVC)\n";
    s += "    return()\n";
    s += "  endif()\n";
    s += "  if (VIX_LINK_FULL_STATIC)\n";
    s += "    target_link_options(${tgt} PRIVATE -static)\n";
    s += "    target_compile_definitions(${tgt} PRIVATE VIX_LINK_FULL_STATIC=1)\n";
    s += "  elseif (VIX_LINK_STATIC)\n";
    s += "    target_link_options(${tgt} PRIVATE -static-libstdc++ -static-libgcc)\n";
    s += "    target_compile_definitions(${tgt} PRIVATE VIX_LINK_STATIC=1)\n";
    s += "  endif()\n";
    s += "endfunction()\n\n";

    s += "vix_apply_static_link_flags(" + projectName + ")\n\n";

    // Sanitizers
    s += "# Sanitizers (mode-aware)\n";
    s += "if (VIX_ENABLE_SANITIZERS AND NOT MSVC)\n";
    s += "  target_compile_options(" + projectName + " PRIVATE -g3 -fno-omit-frame-pointer)\n";
    s += "  target_link_options(" + projectName + " PRIVATE -g)\n\n";
    s += "  if (VIX_SANITIZER_MODE STREQUAL \"ubsan\")\n";
    s += "    target_compile_options(" + projectName + " PRIVATE -O0 -fsanitize=undefined -fno-sanitize-recover=undefined)\n";
    s += "    target_link_options(" + projectName + " PRIVATE -fsanitize=undefined)\n";
    s += "    target_compile_definitions(" + projectName + " PRIVATE VIX_SANITIZERS=1 VIX_UBSAN=1)\n";
    s += "  else()\n";
    s += "    target_compile_options(" + projectName + " PRIVATE -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined)\n";
    s += "    target_link_options(" + projectName + " PRIVATE -fsanitize=address,undefined)\n";
    s += "    target_compile_definitions(" + projectName + " PRIVATE VIX_SANITIZERS=1 VIX_ASAN=1 VIX_UBSAN=1)\n";
    s += "  endif()\n";
    s += "endif()\n\n";

    // Tests
    s += "include(CTest)\n";
    s += "enable_testing()\n\n";

    s += "add_executable(" + projectName + "_basic_test tests/test_basic.cpp)\n";
    s += "target_link_libraries(" + projectName + "_basic_test PRIVATE vix::vix)\n";
    s += "vix_apply_static_link_flags(" + projectName + "_basic_test)\n\n";

    s += "# Apply sanitizer flags to tests too\n";
    s += "if (VIX_ENABLE_SANITIZERS AND NOT MSVC)\n";
    s += "  target_compile_options(" + projectName + "_basic_test PRIVATE -g3 -fno-omit-frame-pointer)\n";
    s += "  target_link_options(" + projectName + "_basic_test PRIVATE -g)\n";
    s += "  if (VIX_SANITIZER_MODE STREQUAL \"ubsan\")\n";
    s += "    target_compile_options(" + projectName + "_basic_test PRIVATE -O0 -fsanitize=undefined -fno-sanitize-recover=undefined)\n";
    s += "    target_link_options(" + projectName + "_basic_test PRIVATE -fsanitize=undefined)\n";
    s += "    target_compile_definitions(" + projectName + "_basic_test PRIVATE VIX_SANITIZERS=1 VIX_UBSAN=1)\n";
    s += "  else()\n";
    s += "    target_compile_options(" + projectName + "_basic_test PRIVATE -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined)\n";
    s += "    target_link_options(" + projectName + "_basic_test PRIVATE -fsanitize=address,undefined)\n";
    s += "    target_compile_definitions(" + projectName + "_basic_test PRIVATE VIX_SANITIZERS=1 VIX_ASAN=1 VIX_UBSAN=1)\n";
    s += "  endif()\n";
    s += "endif()\n\n";

    s += "add_test(NAME " + projectName + ".basic COMMAND " + projectName + "_basic_test)\n\n";

    // Convenience run target
    s += "add_custom_target(run\n";
    s += "  COMMAND $<TARGET_FILE:" + projectName + ">\n";
    s += "  DEPENDS " + projectName + "\n";
    s += "  USES_TERMINAL\n";
    s += ")\n";

    return s;
  }

  std::string make_cmake_presets_json()
  {
    return R"JSON({
  "version": 6,
  "configurePresets": [
    {
      "name": "dev-ninja",
      "displayName": "Dev (Ninja, Debug)",
      "generator": "Ninja",
      "binaryDir": "build-dev-ninja",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "dev-ninja-san",
      "displayName": "Dev (Ninja, ASan+UBSan, Debug)",
      "generator": "Ninja",
      "binaryDir": "build-dev-ninja-san",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
        "VIX_ENABLE_SANITIZERS": "ON",
        "VIX_SANITIZER_MODE": "asan_ubsan"
      }
    },
    {
      "name": "dev-ninja-ubsan",
      "displayName": "Dev (Ninja, UBSan, Debug)",
      "generator": "Ninja",
      "binaryDir": "build-dev-ninja-ubsan",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
        "VIX_ENABLE_SANITIZERS": "ON",
        "VIX_SANITIZER_MODE": "ubsan"
      }
    },

    {
      "name": "release",
      "displayName": "Release (Ninja, Release)",
      "generator": "Ninja",
      "binaryDir": "build-release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },

    {
      "name": "dev-msvc",
      "displayName": "Dev (MSVC, Release)",
      "generator": "Visual Studio 17 2022",
      "architecture": { "value": "x64" },
      "binaryDir": "build-msvc",
      "cacheVariables": {
        "CMAKE_CONFIGURATION_TYPES": "Release"
      }
    }
  ],

  "buildPresets": [
    {
      "name": "build-dev-ninja",
      "displayName": "Build (ALL, Dev Ninja)",
      "configurePreset": "dev-ninja"
    },
    {
      "name": "build-dev-ninja-san",
      "displayName": "Build (ALL, Dev Ninja, ASan+UBSan)",
      "configurePreset": "dev-ninja-san"
    },
    {
      "name": "build-dev-ninja-ubsan",
      "displayName": "Build (ALL, Dev Ninja, UBSan)",
      "configurePreset": "dev-ninja-ubsan"
    },

    {
      "name": "build-release",
      "displayName": "Build (ALL, Release Ninja)",
      "configurePreset": "release"
    },

    {
      "name": "run-dev-ninja",
      "displayName": "Run (target=run, Dev Ninja)",
      "configurePreset": "dev-ninja",
      "targets": ["run"]
    },
    {
      "name": "run-release",
      "displayName": "Run (target=run, Release Ninja)",
      "configurePreset": "release",
      "targets": ["run"]
    },

    {
      "name": "build-msvc",
      "displayName": "Build (ALL, MSVC)",
      "configurePreset": "dev-msvc",
      "configuration": "Release"
    },
    {
      "name": "run-msvc",
      "displayName": "Run (target=run, MSVC)",
      "configurePreset": "dev-msvc",
      "configuration": "Release",
      "targets": ["run"]
    },

    {
      "name": "dev-ninja",
      "displayName": "Alias: dev-ninja → build",
      "configurePreset": "dev-ninja"
    },
    {
      "name": "dev-ninja-san",
      "displayName": "Alias: dev-ninja-san → build",
      "configurePreset": "dev-ninja-san"
    },
    {
      "name": "dev-ninja-ubsan",
      "displayName": "Alias: dev-ninja-ubsan → build",
      "configurePreset": "dev-ninja-ubsan"
    },

    {
      "name": "release",
      "displayName": "Alias: release → build",
      "configurePreset": "release"
    },

    {
      "name": "dev-msvc",
      "displayName": "Alias: dev-msvc → build (MSVC)",
      "configurePreset": "dev-msvc",
      "configuration": "Release"
    }
  ]
}
)JSON";
  }

} // namespace

namespace vix::commands::NewCommand
{
  int run(const std::vector<std::string> &args)
  {
    if (args.empty())
    {
      error("Missing project name.");
      hint("Usage: vix new <name|path> [-d|--dir <base_dir>]");
      return 1;
    }

    const std::string nameOrPath = args[0];
    std::optional<std::string> baseOpt = pick_dir_opt(args);

    try
    {
      fs::path dest;

      if (baseOpt.has_value())
      {
        fs::path base = fs::path(*baseOpt);
        if (!fs::exists(base) || !fs::is_directory(base))
        {
          error("Base directory '" + base.string() + "' is not a valid folder.");
          hint("Make sure it exists and is a directory, or omit --dir to use the current directory.");
          return 2;
        }

        fs::path np = fs::path(nameOrPath);
        dest = np.is_absolute() ? np : (fs::canonical(base) / np);
      }
      else
      {
        fs::path np = fs::path(nameOrPath);
        dest = np.is_absolute() ? np : (fs::current_path() / np);
      }

      const fs::path projectDir = dest;
      const fs::path srcDir = projectDir / "src";
      const fs::path mainCpp = srcDir / "main.cpp";
      const fs::path testsDir = projectDir / "tests";
      const fs::path testCpp = testsDir / "test_basic.cpp";
      const fs::path cmakeLists = projectDir / "CMakeLists.txt";
      const fs::path readmeFile = projectDir / "README.md";
      const fs::path presetsFile = projectDir / "CMakePresets.json";
      const fs::path makefilePath = projectDir / "Makefile";

      if (fs::exists(projectDir) && !is_dir_empty(projectDir))
      {
        error("Cannot create project in '" + projectDir.string() +
              "': directory is not empty.");
        hint("Choose an empty folder or a different project name.");
        return 3;
      }

      fs::create_directories(srcDir);
      fs::create_directories(testsDir);
      write_text_file(mainCpp, kMainCpp);
      write_text_file(testCpp, kBasicTestCpp);
      write_text_file(cmakeLists, make_cmakelists(projectDir.filename().string()));
      write_text_file(readmeFile, make_readme(projectDir.filename().string()));
      write_text_file(presetsFile, make_cmake_presets_json());

      const std::string projName = projectDir.filename().string();

      success("Project '" + projName + "' created.");
      info("Location: " + projectDir.string());
      std::cout << "\n";
      info("Next steps:");
      step("cd \"" + projectDir.string() + "\"");
      step("vix build");
      step("vix run");
      std::cout << "\n";

      return 0;
    }
    catch (const std::exception &ex)
    {
      error("Something went wrong while creating the project.");
      hint(std::string("Details: ") + ex.what());
      return 4;
    }
  }

  int help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix new <name|path> [options]\n";
    out << "\n";

    out << "Description:\n";
    out << "  Scaffold a new Vix.cpp project in the given directory.\n";
    out << "\n";

    out << "Options:\n";
    out << "  -d, --dir <base_dir>    Base directory where the project folder will be created\n";
    out << "                          (default: current working directory)\n";
    out << "\n";

    out << "Examples:\n";
    out << "  vix new api\n";
    out << "  vix new blog -d ./projects\n";
    out << "  vix new /absolute/path/to/app\n";
    out << "\n";

    return 0;
  }

} // namespace vix::commands::NewCommand
