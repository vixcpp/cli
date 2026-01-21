/**
 *
 *  @file NewCommand.cpp
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
#include <algorithm>

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
    app.get("/", [](Request&, Response& res) {
        res.send("Hello world");
    });

    app.run(8080);
}
)";

  constexpr const char *kBasicTestCpp_App = R"(#include <iostream>

int main()
{
    std::cout << "basic test OK\n";
    return 0;
}
)";

  static std::string make_lib_header(const std::string &name)
  {
    std::string s;
    s.reserve(2000);

    s += "#pragma once\n";
    s += "#include <cstddef>\n";
    s += "#include <vector>\n\n";
    s += "namespace " + name + "\n";
    s += "{\n";
    s += "  struct Node\n";
    s += "  {\n";
    s += "    std::size_t id{};\n";
    s += "    std::vector<std::size_t> children{};\n";
    s += "  };\n\n";
    s += "  inline std::vector<Node> make_chain(std::size_t n)\n";
    s += "  {\n";
    s += "    std::vector<Node> nodes;\n";
    s += "    nodes.reserve(n);\n\n";
    s += "    for (std::size_t i = 0; i < n; ++i)\n";
    s += "      nodes.push_back(Node{i, {}});\n\n";
    s += "    for (std::size_t i = 0; i + 1 < n; ++i)\n";
    s += "      nodes[i].children.push_back(i + 1);\n\n";
    s += "    return nodes;\n";
    s += "  }\n";
    s += "}\n";

    return s;
  }

  static std::string make_basic_test_cpp_lib(const std::string &name)
  {
    std::string s;
    s.reserve(1200);

    s += "#include <" + name + "/" + name + ".hpp>\n";
    s += "#include <iostream>\n\n";
    s += "int main()\n";
    s += "{\n";
    s += "  auto nodes = " + name + "::make_chain(5);\n";
    s += "  std::cout << \"nodes=\" << nodes.size() << \"\\n\";\n";
    s += "  return nodes.size() == 5 ? 0 : 1;\n";
    s += "}\n";

    return s;
  }

  static std::string make_readme_app(const std::string &projectName)
  {
    std::string readme;
    readme.reserve(22000);

    readme += "# " + projectName + " — Example project using [Vix.cpp](https://github.com/vixcpp/vix)\n\n";
    readme += projectName + " is a minimal example showing how to **build**, **run**, **test**, and **hot-reload** a C++ app with **Vix.cpp**.\n";
    readme += "It uses `CMakePresets.json` for a clean cross-platform workflow, supports optional **Vix ORM**, and includes a basic **CTest** test out of the box.\n\n";
    readme += "---\n\n";

    readme += "## Features\n\n";
    readme += "- Simple **HTTP server** powered by `vix::App`\n";
    readme += "- Modern **C++20** codebase\n";
    readme += "- Cross-platform build workflow via **CMake presets**\n";
    readme += "- **Hot reload** dev mode: rebuild & restart on save (`vix dev`)\n";
    readme += "- Built-in **tests** (CTest) via `vix tests`\n";
    readme += "- Optional sanitizers (`--san`, `--ubsan`)\n";
    readme += "- Optional **ORM** (`VIX_USE_ORM=ON`)\n\n";
    readme += "---\n\n";

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

    readme += "## Requirements\n\n";
    readme += "- **CMake ≥ 3.20**\n";
    readme += "- **C++20 compiler**\n";
    readme += "  - Linux/macOS: Clang ≥ 15 or GCC ≥ 11\n";
    readme += "  - Windows: Visual Studio 2022 (MSVC 19.3+)\n";
    readme += "- **Vix.cpp installed** (system install or local build)\n\n";
    readme += "---\n\n";

    readme += "## Quick start\n\n";
    readme += "```bash\n";
    readme += "vix build\n";
    readme += "vix run\n";
    readme += "```\n\n";
    readme += "Open **http://localhost:8080/** in your browser.\n\n";
    readme += "---\n\n";

    readme += "## Vix CLI workflow\n\n";
    readme += "```bash\n";
    readme += "vix build                 # Configure & build (presets)\n";
    readme += "vix run                   # Run (builds if needed)\n";
    readme += "vix dev                   # Hot reload: watch + rebuild + restart\n";
    readme += "vix check                 # Validate project build\n";
    readme += "vix tests                 # Run tests (alias of `vix check --tests`)\n";
    readme += "```\n\n";

    readme += "## Tests\n\n";
    readme += "This project uses **CTest**. Vix provides a dedicated command:\n\n";
    readme += "```bash\n";
    readme += "vix tests                 # Run all tests\n";
    readme += "vix tests --list          # List tests (ctest --show-only)\n";
    readme += "vix tests --fail-fast     # Stop at first failure (ctest --stop-on-failure)\n";
    readme += "```\n\n";

    readme += "## Enable ORM (optional)\n\n";
    readme += "```bash\n";
    readme += "vix build -D VIX_USE_ORM=ON\n";
    readme += "vix run   -D VIX_USE_ORM=ON\n";
    readme += "vix dev   -D VIX_USE_ORM=ON\n";
    readme += "```\n\n";
    readme += "---\n\n";

    readme += "## License\n\n";
    readme += "MIT © [Vix.cpp Authors](https://github.com/vixcpp)\n";

    return readme;
  }

  static std::string make_readme_lib(const std::string &name)
  {
    std::string readme;
    readme.reserve(18000);

    readme += "# " + name + " — C++ library for [Vix.cpp](https://github.com/vixcpp/vix)\n\n";
    readme += name + " is a minimal **header-only** C++ library scaffold generated by **Vix CLI**.\n\n";
    readme += "This template is intentionally registry-friendly: it is easy to **tag** and **publish** into the **Vix Registry** so other developers can `vix add` it.\n\n";
    readme += "---\n\n";

    readme += "## Project Structure\n\n";
    readme += "```\n";
    readme += name + "/\n";
    readme += "├── CMakeLists.txt\n";
    readme += "├── CMakePresets.json\n";
    readme += "├── README.md\n";
    readme += "├── vix.json\n";
    readme += "├── include/\n";
    readme += "│   └── " + name + "/\n";
    readme += "│       └── " + name + ".hpp\n";
    readme += "└── tests/\n";
    readme += "    └── test_basic.cpp\n";
    readme += "```\n\n";
    readme += "---\n\n";

    readme += "## Build & test\n\n";
    readme += "```bash\n";
    readme += "vix tests\n";
    readme += "```\n\n";
    readme += "---\n\n";

    readme += "## Publish to the Vix Registry\n\n";
    readme += "1) Initialize git + first tag:\n\n";
    readme += "```bash\n";
    readme += "git init\n";
    readme += "git add .\n";
    readme += "git commit -m \"init: " + name + "\"\n";
    readme += "git tag v0.1.0\n";
    readme += "```\n\n";
    readme += "2) Publish (generates registry JSON + PR workflow):\n\n";
    readme += "```bash\n";
    readme += "vix publish 0.1.0\n";
    readme += "```\n\n";
    readme += "---\n\n";

    readme += "## Install (once published)\n\n";
    readme += "```bash\n";
    readme += "vix registry sync\n";
    readme += "vix add <namespace>/" + name + "@0.1.0\n";
    readme += "```\n\n";
    readme += "---\n\n";

    readme += "## License\n\n";
    readme += "MIT © [Vix.cpp Authors](https://github.com/vixcpp)\n";

    return readme;
  }

  static std::string make_cmakelists_app(const std::string &projectName)
  {
    std::string s;
    s.reserve(16000);

    s += "cmake_minimum_required(VERSION 3.20)\n";
    s += "project(" + projectName + " LANGUAGES CXX)\n\n";

    s += "set(CMAKE_CXX_STANDARD 20)\n";
    s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

    s += "option(VIX_ENABLE_SANITIZERS \"Enable ASan/UBSan (dev only)\" OFF)\n";
    s += "option(VIX_USE_ORM \"Enable Vix ORM (requires vix::orm in install)\" OFF)\n";
    s += "option(VIX_LINK_STATIC \"Static libstdc++/libgcc (glibc-safe)\" OFF)\n";
    s += "option(VIX_LINK_FULL_STATIC \"Full static link (-static). Prefer musl.\" OFF)\n";
    s += "set(VIX_SANITIZER_MODE \"asan_ubsan\" CACHE STRING \"Sanitizer mode: asan_ubsan | ubsan\")\n\n";

    s += "find_package(vix QUIET CONFIG)\n";
    s += "if (NOT vix_FOUND)\n";
    s += "  find_package(Vix CONFIG REQUIRED)\n";
    s += "endif()\n\n";

    s += "add_executable(" + projectName + " src/main.cpp)\n";
    s += "target_link_libraries(" + projectName + " PRIVATE vix::vix)\n\n";

    s += "if (VIX_USE_ORM)\n";
    s += "  if (TARGET vix::orm)\n";
    s += "    target_link_libraries(" + projectName + " PRIVATE vix::orm)\n";
    s += "    target_compile_definitions(" + projectName + " PRIVATE VIX_USE_ORM=1)\n";
    s += "  else()\n";
    s += "    message(FATAL_ERROR \"VIX_USE_ORM=ON but vix::orm target is not available in this Vix install\")\n";
    s += "  endif()\n";
    s += "endif()\n\n";

    s += "if (MSVC)\n";
    s += "  target_compile_options(" + projectName + " PRIVATE /W4 /permissive-)\n";
    s += "else()\n";
    s += "  target_compile_options(" + projectName + " PRIVATE -Wall -Wextra -Wpedantic)\n";
    s += "endif()\n\n";

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

    s += "if (VIX_ENABLE_SANITIZERS AND NOT MSVC)\n";
    s += "  target_compile_options(" + projectName + " PRIVATE -g3 -fno-omit-frame-pointer)\n";
    s += "  target_link_options(" + projectName + " PRIVATE -g)\n";
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

    s += "include(CTest)\n";
    s += "enable_testing()\n\n";

    s += "add_executable(" + projectName + "_basic_test tests/test_basic.cpp)\n";
    s += "target_link_libraries(" + projectName + "_basic_test PRIVATE vix::vix)\n";
    s += "vix_apply_static_link_flags(" + projectName + "_basic_test)\n\n";

    s += "add_test(NAME " + projectName + ".basic COMMAND " + projectName + "_basic_test)\n\n";

    s += "add_custom_target(run\n";
    s += "  COMMAND $<TARGET_FILE:" + projectName + ">\n";
    s += "  DEPENDS " + projectName + "\n";
    s += "  USES_TERMINAL\n";
    s += ")\n";

    return s;
  }

  static std::string make_cmakelists_lib(const std::string &name)
  {
    std::string s;
    s.reserve(12000);

    s += "cmake_minimum_required(VERSION 3.20)\n";
    s += "project(" + name + " LANGUAGES CXX)\n\n";

    s += "set(CMAKE_CXX_STANDARD 20)\n";
    s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

    s += "# Header-only library (INTERFACE)\n";
    s += "add_library(" + name + " INTERFACE)\n";
    s += "add_library(" + name + "::" + name + " ALIAS " + name + ")\n\n";

    s += "target_include_directories(" + name + " INTERFACE\n";
    s += "  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>\n";
    s += "  $<INSTALL_INTERFACE:include>\n";
    s += ")\n\n";

    s += "if (MSVC)\n";
    s += "  target_compile_options(" + name + " INTERFACE /W4 /permissive-)\n";
    s += "else()\n";
    s += "  target_compile_options(" + name + " INTERFACE -Wall -Wextra -Wpedantic)\n";
    s += "endif()\n\n";

    s += "include(CTest)\n";
    s += "enable_testing()\n\n";

    s += "add_executable(" + name + "_basic_test tests/test_basic.cpp)\n";
    s += "target_link_libraries(" + name + "_basic_test PRIVATE " + name + "::" + name + ")\n";
    s += "add_test(NAME " + name + ".basic COMMAND " + name + "_basic_test)\n";

    return s;
  }

  std::string make_cmake_presets_json_app()
  {
    return R"JSON({
  "version": 6,

  "configurePresets": [
    {
      "name": "dev-ninja",
      "displayName": "Dev (Ninja, Debug)",
      "generator": "Ninja",
      "binaryDir": "build-ninja",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "dev-ninja-san",
      "displayName": "Dev (Ninja, ASan+UBSan, Debug)",
      "generator": "Ninja",
      "binaryDir": "build-ninja-san",
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
      "binaryDir": "build-ninja-ubsan",
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
      "name": "build-ninja",
      "displayName": "Build (ALL, Ninja Debug)",
      "configurePreset": "dev-ninja"
    },
    {
      "name": "build-ninja-san",
      "displayName": "Build (ALL, Ninja Debug, ASan+UBSan)",
      "configurePreset": "dev-ninja-san"
    },
    {
      "name": "build-ninja-ubsan",
      "displayName": "Build (ALL, Ninja Debug, UBSan)",
      "configurePreset": "dev-ninja-ubsan"
    },

    {
      "name": "build-release",
      "displayName": "Build (ALL, Ninja Release)",
      "configurePreset": "release"
    },

    {
      "name": "run-dev-ninja",
      "displayName": "Run (target=run, Ninja Debug)",
      "configurePreset": "dev-ninja",
      "targets": ["run"]
    },
    {
      "name": "run-release",
      "displayName": "Run (target=run, Ninja Release)",
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
      "displayName": "Alias: dev-ninja → build-ninja",
      "configurePreset": "dev-ninja"
    },
    {
      "name": "dev-ninja-san",
      "displayName": "Alias: dev-ninja-san → build-ninja-san",
      "configurePreset": "dev-ninja-san"
    },
    {
      "name": "dev-ninja-ubsan",
      "displayName": "Alias: dev-ninja-ubsan → build-ninja-ubsan",
      "configurePreset": "dev-ninja-ubsan"
    },

    {
      "name": "release",
      "displayName": "Alias: release → build-release",
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

  static std::string make_cmake_presets_json_lib()
  {
    return R"JSON({
  "version": 6,

  "configurePresets": [
    {
      "name": "dev-ninja",
      "displayName": "Dev (Ninja, Debug)",
      "generator": "Ninja",
      "binaryDir": "build-ninja",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "dev-ninja-san",
      "displayName": "Dev (Ninja, ASan+UBSan, Debug)",
      "generator": "Ninja",
      "binaryDir": "build-ninja-san",
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
      "binaryDir": "build-ninja-ubsan",
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
    { "name": "build-ninja", "displayName": "Build (ALL, Ninja Debug)", "configurePreset": "dev-ninja" },
    { "name": "build-ninja-san", "displayName": "Build (ALL, Ninja Debug, ASan+UBSan)", "configurePreset": "dev-ninja-san" },
    { "name": "build-ninja-ubsan", "displayName": "Build (ALL, Ninja Debug, UBSan)", "configurePreset": "dev-ninja-ubsan" },

    { "name": "build-release", "displayName": "Build (ALL, Ninja Release)", "configurePreset": "release" },

    { "name": "build-msvc", "displayName": "Build (ALL, MSVC)", "configurePreset": "dev-msvc", "configuration": "Release" },

    { "name": "dev-ninja", "displayName": "Alias: dev-ninja → build-ninja", "configurePreset": "dev-ninja" },
    { "name": "dev-ninja-san", "displayName": "Alias: dev-ninja-san → build-ninja-san", "configurePreset": "dev-ninja-san" },
    { "name": "dev-ninja-ubsan", "displayName": "Alias: dev-ninja-ubsan → build-ninja-ubsan", "configurePreset": "dev-ninja-ubsan" },
    { "name": "release", "displayName": "Alias: release → build-release", "configurePreset": "release" },

    { "name": "dev-msvc", "displayName": "Alias: dev-msvc → build (MSVC)", "configurePreset": "dev-msvc", "configuration": "Release" }
  ]
}
)JSON";
  }

  static std::string make_project_manifest_app(const std::string &name)
  {
    return "version = 1\n\n"
           "[app]\n"
           "kind = \"project\"\n"
           "dir = \".\"\n"
           "name = \"" +
           name + "\"\n"
                  "entry = \"src/main.cpp\"\n\n"
                  "[build]\n"
                  "preset = \"dev-ninja\"\n"
                  "run_preset = \"run-dev-ninja\"\n"
                  "jobs = 8\n\n"
                  "[dev]\n"
                  "watch = true\n"
                  "force = \"server\"\n"
                  "clear = \"auto\"\n\n"
                  "[logging]\n"
                  "level = \"info\"\n"
                  "format = \"json-pretty\"\n"
                  "color = \"auto\"\n\n"
                  "[run]\n"
                  "args = [\"--port\",\"8080\"]\n"
                  "env = []\n"
                  "timeout_sec = 0\n";
  }

  static std::string make_project_manifest_lib(const std::string &name)
  {
    return "version = 1\n\n"
           "[app]\n"
           "kind = \"project\"\n"
           "dir = \".\"\n"
           "name = \"" +
           name + "\"\n"
                  "entry = \"tests/test_basic.cpp\"\n\n"
                  "[build]\n"
                  "preset = \"dev-ninja\"\n"
                  "jobs = 8\n\n"
                  "[dev]\n"
                  "watch = true\n"
                  "force = \"tests\"\n"
                  "clear = \"auto\"\n\n"
                  "[logging]\n"
                  "level = \"info\"\n"
                  "format = \"json-pretty\"\n"
                  "color = \"auto\"\n\n"
                  "[run]\n"
                  "args = []\n"
                  "env = []\n"
                  "timeout_sec = 0\n";
  }

  static std::string make_vix_json_lib(const std::string &name)
  {
    std::string s;
    s.reserve(1500);

    s += "{\n";
    s += "  \"name\": \"" + name + "\",\n";
    s += "  \"namespace\": \"<your-namespace>\",\n";
    s += "  \"version\": \"0.1.0\",\n";
    s += "  \"license\": \"MIT\",\n";
    s += "  \"description\": \"A C++ library.\",\n";
    s += "  \"repo\": \"<git-url>\",\n";
    s += "  \"type\": \"library\",\n";
    s += "  \"build\": {\n";
    s += "    \"system\": \"cmake\",\n";
    s += "    \"preset\": \"dev-ninja\"\n";
    s += "  }\n";
    s += "}\n";

    return s;
  }

  static bool consume_flag(std::vector<std::string> &a, const std::string &flag)
  {
    auto it = std::find(a.begin(), a.end(), flag);
    if (it == a.end())
      return false;
    a.erase(it);
    return true;
  }

  static bool has_any(const std::vector<std::string> &a, const std::vector<std::string> &candidates)
  {
    for (const auto &x : candidates)
    {
      if (std::find(a.begin(), a.end(), x) != a.end())
        return true;
    }
    return false;
  }

} // namespace

namespace vix::commands::NewCommand
{
  int run(const std::vector<std::string> &argsIn)
  {
    if (argsIn.empty())
    {
      error("Missing project name.");
      hint("Usage: vix new <name|path> [-d|--dir <base_dir>] [--lib]");
      return 1;
    }

    std::vector<std::string> args = argsIn;

    const bool isLib = has_any(args, {"--lib", "--library", "--type=lib", "--type=library"});
    consume_flag(args, "--lib");
    consume_flag(args, "--library");
    consume_flag(args, "--type=lib");
    consume_flag(args, "--type=library");

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
      const std::string projName = projectDir.filename().string();

      if (fs::exists(projectDir) && !is_dir_empty(projectDir))
      {
        error("Cannot create project in '" + projectDir.string() + "': directory is not empty.");
        hint("Choose an empty folder or a different project name.");
        return 3;
      }

      const fs::path cmakeLists = projectDir / "CMakeLists.txt";
      const fs::path readmeFile = projectDir / "README.md";
      const fs::path presetsFile = projectDir / "CMakePresets.json";
      const fs::path manifestPath = projectDir / (projName + ".vix");

      if (!isLib)
      {
        const fs::path srcDir = projectDir / "src";
        const fs::path mainCpp = srcDir / "main.cpp";
        const fs::path testsDir = projectDir / "tests";
        const fs::path testCpp = testsDir / "test_basic.cpp";

        fs::create_directories(srcDir);
        fs::create_directories(testsDir);

        write_text_file(mainCpp, kMainCpp);
        write_text_file(testCpp, kBasicTestCpp_App);

        write_text_file(cmakeLists, make_cmakelists_app(projName));
        write_text_file(readmeFile, make_readme_app(projName));
        if (!isLib)
          write_text_file(presetsFile, make_cmake_presets_json_app());
        else
          write_text_file(presetsFile, make_cmake_presets_json_lib());

        write_text_file(manifestPath, make_project_manifest_app(projName));

        success("Project '" + projName + "' created.");
        info("Location: " + projectDir.string());
        std::cout << "\n";
        info("Next steps:");
        step("cd \"" + projectDir.string() + "\"");
        step("vix " + projName + ".vix");
        step("vix dev " + projName + ".vix");
        step("vix build");
        step("vix run");
        std::cout << "\n";
        return 0;
      }

      const fs::path includeDir = projectDir / "include" / projName;
      const fs::path headerFile = includeDir / (projName + ".hpp");
      const fs::path testsDir = projectDir / "tests";
      const fs::path testCpp = testsDir / "test_basic.cpp";
      const fs::path vixJson = projectDir / "vix.json";

      fs::create_directories(includeDir);
      fs::create_directories(testsDir);

      write_text_file(headerFile, make_lib_header(projName));
      write_text_file(testCpp, make_basic_test_cpp_lib(projName));

      write_text_file(cmakeLists, make_cmakelists_lib(projName));
      write_text_file(readmeFile, make_readme_lib(projName));
      if (!isLib)
        write_text_file(presetsFile, make_cmake_presets_json_app());
      else
        write_text_file(presetsFile, make_cmake_presets_json_lib());

      write_text_file(vixJson, make_vix_json_lib(projName));
      write_text_file(manifestPath, make_project_manifest_lib(projName));

      success("Project '" + projName + "' created.");
      info("Location: " + projectDir.string());
      std::cout << "\n";
      info("Template: library (header-only)");
      std::cout << "\n";
      info("Next steps:");
      step("cd \"" + projectDir.string() + "\"");
      step("vix tests");
      step("git init");
      step("git add . && git commit -m \"init: " + projName + "\"");
      step("git tag v0.1.0");
      step("vix publish 0.1.0");
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
    out << "  By default, generates an application template.\n";
    out << "\n";

    out << "Options:\n";
    out << "  -d, --dir <base_dir>    Base directory where the project folder will be created\n";
    out << "                          (default: current working directory)\n";
    out << "  --lib                   Generate a library template (header-only, registry-friendly)\n";
    out << "\n";

    out << "Examples:\n";
    out << "  vix new api\n";
    out << "  vix new tree --lib\n";
    out << "  vix new blog -d ./projects\n";
    out << "  vix new /absolute/path/to/app\n";
    out << "\n";

    return 0;
  }

} // namespace vix::commands::NewCommand
