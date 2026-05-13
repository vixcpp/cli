/**
 * @file NewTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/NewTemplates.hpp>

namespace vix::commands::new_cmd::templates
{

  // ------------------------------------------------------------------
  // Library scaffold helpers
  // ------------------------------------------------------------------

  std::string make_lib_header(const std::string &name)
  {
    std::string s;
    s.reserve(1500);

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

  std::string make_basic_test_cpp_lib(const std::string &name)
  {
    std::string s;
    s.reserve(1200);

    s += "#include <vix/tests/tests.hpp>\n";
    s += "#include <" + name + "/" + name + ".hpp>\n\n";

    s += "int main()\n";
    s += "{\n";
    s += "  using namespace vix::tests;\n\n";
    s += "  auto &registry = TestRegistry::instance();\n";
    s += "  registry.clear();\n\n";

    s += "  registry.add(TestCase(\"" + name + " basic test\", [] {\n";
    s += "    auto nodes = " + name + "::make_chain(5);\n";
    s += "    Assert::equal(nodes.size(), static_cast<std::size_t>(5));\n";
    s += "  }));\n\n";

    s += "  return TestRunner::run_all_and_exit();\n";
    s += "}\n";

    return s;
  }

  std::string make_basic_example_cpp_lib(const std::string &name)
  {
    std::string s;
    s.reserve(700);

    s += "#include <" + name + "/" + name + ".hpp>\n";
    s += "#include <iostream>\n\n";
    s += "int main()\n";
    s += "{\n";
    s += "  auto nodes = " + name + "::make_chain(3);\n";
    s += "  std::cout << \"nodes=\" << nodes.size() << \"\\n\";\n";
    s += "  return 0;\n";
    s += "}\n";

    return s;
  }

  std::string make_examples_cmakelists_lib(const std::string &name)
  {
    return name + "_add_example(" + name + "_example_basic basic.cpp)\n";
  }

  // ------------------------------------------------------------------
  // README generators
  // ------------------------------------------------------------------

  std::string make_readme_app(const std::string &projectName)
  {
    std::string readme;
    readme.reserve(8000);

    readme += "# " + projectName + "\n\n";
    readme += "Minimal Vix.cpp application.\n\n";

    readme += "## Quick start\n\n";
    readme += "```bash\n";
    readme += "cd " + projectName + "\n";
    readme += "cp .env.example .env\n";
    readme += "vix build\n";
    readme += "vix run\n";
    readme += "```\n\n";

    readme += "Then open:\n\n";
    readme += "```\n";
    readme += "http://localhost:8080\n";
    readme += "```\n\n";

    readme += "## Dependencies\n\n";
    readme += "This project uses a `vix.json` manifest.\n\n";
    readme += "Workflow:\n\n";
    readme += "- `vix add <pkg>` → add dependency\n";
    readme += "- `vix install` → install dependencies\n";
    readme += "- `vix.lock` → ensures reproducible builds\n\n";
    readme += "Example:\n\n";
    readme += "```bash\n";
    readme += "vix add gk/json@^1.0.0\n";
    readme += "vix install\n";
    readme += "```\n\n";

    readme += "## Tasks\n\n";
    readme += "Run project tasks:\n\n";
    readme += "```bash\n";
    readme += "vix task <name>\n";
    readme += "```\n\n";
    readme += "Common tasks:\n\n";
    readme += "```bash\n";
    readme += "vix task dev\n";
    readme += "vix task test\n";
    readme += "vix task ci\n";
    readme += "```\n\n";
    readme += "Edit `vix.json` to customize tasks and pipelines.\n\n";

    readme += "## Configuration\n\n";
    readme += "Vix uses `.env` files for configuration.\n\n";
    readme += "Start by copying the example:\n\n";
    readme += "```bash\n";
    readme += "cp .env.example .env\n";
    readme += "```\n\n";
    readme += "Example:\n\n";
    readme += "```env\n";
    readme += "SERVER_PORT=8080\n";
    readme += "DATABASE_ENGINE=mysql\n";
    readme += "DATABASE_DEFAULT_HOST=127.0.0.1\n";
    readme += "DATABASE_DEFAULT_PORT=3306\n";
    readme += "DATABASE_DEFAULT_USER=root\n";
    readme += "DATABASE_DEFAULT_PASSWORD=\n";
    readme += "DATABASE_DEFAULT_NAME=appdb\n";
    readme += "LOGGING_ASYNC=true\n";
    readme += "WAF_MODE=basic\n";
    readme += "```\n\n";

    readme += "## Using configuration in code\n\n";
    readme += "```cpp\n";
    readme += "#include <vix.hpp>\n";
    readme += "using namespace vix;\n\n";
    readme += "int main()\n";
    readme += "{\n";
    readme += "  config::Config cfg{\".env\"};\n\n";
    readme += "  App app;\n";
    readme += "  app.get(\"/\", [](Request&, Response& res) {\n";
    readme += "    res.send(\"Hello world\");\n";
    readme += "  });\n\n";
    readme += "  app.run(cfg.getServerPort());\n";
    readme += "}\n";
    readme += "```\n\n";

    readme += "## Environment mapping\n\n";
    readme += "Vix maps config keys to environment variables:\n\n";
    readme += "- `server.port` → `SERVER_PORT`\n";
    readme += "- `database.default.host` → `DATABASE_DEFAULT_HOST`\n";
    readme += "- `database.default.name` → `DATABASE_DEFAULT_NAME`\n\n";
    readme += "This keeps the C++ API clean and environment-driven.\n\n";

    readme += "## Environment layers\n\n";
    readme += "You can use multiple env files:\n\n";
    readme += "- `.env`\n";
    readme += "- `.env.local`\n";
    readme += "- `.env.production`\n\n";
    readme += "Use `.env` for development and environment-specific files for deployment.\n";

    return readme;
  }

  std::string make_readme_lib(const std::string &name)
  {
    std::string readme;
    readme.reserve(6500);

    readme += "# " + name + "\n\n";
    readme += "Header-only C++ library scaffold.\n\n";

    readme += "## Principles\n\n";
    readme += "This scaffold is generated to stay deterministic, composable, and registry-safe.\n\n";
    readme += "Key rules:\n\n";
    readme += "- tests are OFF by default\n";
    readme += "- examples are OFF by default\n";
    readme += "- the library exposes a stable alias target\n";
    readme += "- example targets are prefixed to avoid collisions\n";
    readme += "- the package is safe for add_subdirectory(...) and Vix registry integration\n\n";

    readme += "## Targets\n\n";
    readme += "Canonical target:\n\n";
    readme += "```cmake\n";
    readme += name + "::" + name + "\n";
    readme += "```\n\n";

    readme += "## Manifest\n\n";
    readme += "This project includes a `vix.json` manifest.\n\n";
    readme += "For libraries, `vix.json` is used to describe package metadata and declared dependencies.\n\n";
    readme += "Important:\n\n";
    readme += "- `vix.json` stores declared dependency requirements\n";
    readme += "- `vix.lock` stores exact resolved versions for reproducible installs\n";
    readme += "- `vix add` updates both `vix.json` and `vix.lock`\n";
    readme += "- `vix install` installs dependencies from `vix.lock`\n\n";
    readme += "Example:\n\n";
    readme += "```bash\n";
    readme += "vix add gk/json@^1.0.0\n";
    readme += "vix install\n";
    readme += "```\n\n";

    readme += "## Build\n\n";
    readme += "Build project:\n\n";
    readme += "```bash\n";
    readme += "vix build\n";
    readme += "```\n\n";
    readme += "Build with tests enabled:\n\n";
    readme += "```bash\n";
    readme += "vix build -- -D" + name + "_BUILD_TESTS=ON\n";
    readme += "```\n\n";
    readme += "Build with examples enabled:\n\n";
    readme += "```bash\n";
    readme += "vix build -- -D" + name + "_BUILD_EXAMPLES=ON\n";
    readme += "```\n\n";
    readme += "Build tests + examples:\n\n";
    readme += "```bash\n";
    readme += "vix build -- -D" + name + "_BUILD_TESTS=ON -D" + name + "_BUILD_EXAMPLES=ON\n";
    readme += "```\n\n";

    readme += "## Tests\n\n";
    readme += "Tests are disabled by default for header-only libraries.\n\n";
    readme += "Enable them first:\n\n";
    readme += "```bash\n";
    readme += "vix build --build-target all -- -D" + name + "_BUILD_TESTS=ON\n";
    readme += "```\n\n";
    readme += "Then run:\n\n";
    readme += "```bash\n";
    readme += "vix tests\n";
    readme += "```\n\n";

    readme += "## Notes\n\n";
    readme += "- Uses embedded Vix CMake presets (dev, dev-ninja, release)\n";
    readme += "- Automatically configures and builds (no manual cmake needed)\n";
    readme += "- Pass extra CMake flags after `--`\n";
    readme += "- Edit `vix.json` metadata before publishing the package\n";

    return readme;
  }

  // ------------------------------------------------------------------
  // CMakePresets.json generators
  // ------------------------------------------------------------------

  std::string make_cmake_presets_json_app(const FeaturesSelection &f)
  {
    std::string json;
    json.reserve(9000);

    json += "{\n";
    json += "  \"version\": 6,\n\n";
    json += "  \"configurePresets\": [\n";
    json += "    {\n";
    json += "      \"name\": \"dev-ninja\",\n";
    json += "      \"displayName\": \"Dev (Ninja, Debug)\",\n";
    json += "      \"generator\": \"Ninja\",\n";
    json += "      \"binaryDir\": \"build-ninja\",\n";
    json += "      \"cacheVariables\": {\n";
    json += "        \"CMAKE_BUILD_TYPE\": \"Debug\",\n";
    json += "        \"CMAKE_EXPORT_COMPILE_COMMANDS\": \"ON\"";

    if (f.orm)
      json += ",\n        \"VIX_USE_ORM\": \"ON\"";
    if (f.static_rt)
      json += ",\n        \"VIX_LINK_STATIC\": \"ON\"";
    if (f.full_static)
      json += ",\n        \"VIX_LINK_FULL_STATIC\": \"ON\"";

    json += "\n      }\n";
    json += "    }";

    if (f.sanitizers)
    {
      json += ",\n";
      json += "    {\n";
      json += "      \"name\": \"dev-ninja-san\",\n";
      json += "      \"displayName\": \"Dev (Ninja, ASan+UBSan, Debug)\",\n";
      json += "      \"generator\": \"Ninja\",\n";
      json += "      \"binaryDir\": \"build-ninja-san\",\n";
      json += "      \"cacheVariables\": {\n";
      json += "        \"CMAKE_BUILD_TYPE\": \"Debug\",\n";
      json += "        \"CMAKE_EXPORT_COMPILE_COMMANDS\": \"ON\",\n";
      json += "        \"VIX_ENABLE_SANITIZERS\": \"ON\"";
      if (f.orm)
        json += ",\n        \"VIX_USE_ORM\": \"ON\"";
      if (f.static_rt)
        json += ",\n        \"VIX_LINK_STATIC\": \"ON\"";
      if (f.full_static)
        json += ",\n        \"VIX_LINK_FULL_STATIC\": \"ON\"";
      json += "\n      }\n";
      json += "    }";
    }

    json += ",\n";
    json += "    {\n";
    json += "      \"name\": \"release\",\n";
    json += "      \"displayName\": \"Release (Ninja, Release)\",\n";
    json += "      \"generator\": \"Ninja\",\n";
    json += "      \"binaryDir\": \"build-release\",\n";
    json += "      \"cacheVariables\": {\n";
    json += "        \"CMAKE_BUILD_TYPE\": \"Release\",\n";
    json += "        \"CMAKE_EXPORT_COMPILE_COMMANDS\": \"ON\"";
    if (f.orm)
      json += ",\n        \"VIX_USE_ORM\": \"ON\"";
    if (f.static_rt)
      json += ",\n        \"VIX_LINK_STATIC\": \"ON\"";
    if (f.full_static)
      json += ",\n        \"VIX_LINK_FULL_STATIC\": \"ON\"";
    json += "\n      }\n";
    json += "    },\n";
    json += "    {\n";
    json += "      \"name\": \"dev-msvc\",\n";
    json += "      \"displayName\": \"Dev (MSVC, Release)\",\n";
    json += "      \"generator\": \"Visual Studio 17 2022\",\n";
    json += "      \"architecture\": { \"value\": \"x64\" },\n";
    json += "      \"binaryDir\": \"build-msvc\",\n";
    json += "      \"cacheVariables\": {\n";
    json += "        \"CMAKE_CONFIGURATION_TYPES\": \"Release\"";
    if (f.orm)
      json += ",\n        \"VIX_USE_ORM\": \"ON\"";
    json += "\n      }\n";
    json += "    }\n";
    json += "  ],\n\n";

    json += "  \"buildPresets\": [\n";
    json += "    { \"name\": \"build-ninja\", \"displayName\": \"Build (ALL, Ninja Debug)\", \"configurePreset\": \"dev-ninja\" }";
    if (f.sanitizers)
      json += ",\n    { \"name\": \"build-ninja-san\", \"displayName\": \"Build (ALL, Ninja Debug, ASan+UBSan)\", \"configurePreset\": \"dev-ninja-san\" }";
    json += ",\n    { \"name\": \"build-release\", \"displayName\": \"Build (ALL, Ninja Release)\", \"configurePreset\": \"release\" },\n";
    json += "    { \"name\": \"run-dev-ninja\", \"displayName\": \"Run (target=run, Ninja Debug)\", \"configurePreset\": \"dev-ninja\", \"targets\": [\"run\"] },\n";
    json += "    { \"name\": \"run-release\", \"displayName\": \"Run (target=run, Ninja Release)\", \"configurePreset\": \"release\", \"targets\": [\"run\"] },\n";
    json += "    { \"name\": \"build-msvc\", \"displayName\": \"Build (ALL, MSVC)\", \"configurePreset\": \"dev-msvc\", \"configuration\": \"Release\" },\n";
    json += "    { \"name\": \"run-msvc\", \"displayName\": \"Run (target=run, MSVC)\", \"configurePreset\": \"dev-msvc\", \"configuration\": \"Release\", \"targets\": [\"run\"] }\n";
    json += "  ]\n";
    json += "}\n";

    return json;
  }

  std::string make_cmake_presets_json_lib()
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
    { "name": "build-release", "displayName": "Build (ALL, Ninja Release)", "configurePreset": "release" },
    { "name": "build-msvc", "displayName": "Build (ALL, MSVC)", "configurePreset": "dev-msvc", "configuration": "Release" }
  ]
}
)JSON";
  }

  // ------------------------------------------------------------------
  // Project manifest (.vix) generators
  // ------------------------------------------------------------------

  std::string make_project_manifest_app(const std::string &name, const FeaturesSelection &f)
  {
    std::string s;
    s.reserve(800);

    s += "version = 1\n\n";
    s += "[app]\n";
    s += "kind = \"project\"\n";
    s += "dir = \".\"\n";
    s += "name = \"" + name + "\"\n";
    s += "entry = \"src/main.cpp\"\n\n";
    s += "[build]\n";
    s += "preset = \"dev-ninja\"\n";
    s += "run_preset = \"run-dev-ninja\"\n";

    if (f.orm || f.sanitizers || f.static_rt || f.full_static)
    {
      s += "\n[features]\n";
      if (f.orm)
        s += "orm = true\n";
      if (f.sanitizers)
        s += "sanitizers = true\n";
      if (f.static_rt)
        s += "static_rt = true\n";
      if (f.full_static)
        s += "full_static = true\n";
    }

    return s;
  }

  std::string make_project_manifest_lib(const std::string &name)
  {
    return "version = 1\n\n"
           "[app]\n"
           "kind = \"project\"\n"
           "dir = \".\"\n"
           "name = \"" +
           name + "\"\n"
                  "entry = \"tests/test_basic.cpp\"\n\n"
                  "[build]\n"
                  "preset = \"dev-ninja\"\n";
  }

  // ------------------------------------------------------------------
  // vix.json generators
  // ------------------------------------------------------------------

  std::string make_vix_json_lib(const std::string &name)
  {
    std::string s;
    s.reserve(1600);

    s += "{\n";
    s += "  \"name\": \"" + name + "\",\n";
    s += "  \"namespace\": \"your-namespace\",\n";
    s += "  \"version\": \"0.1.0\",\n";
    s += "  \"type\": \"header-only\",\n";
    s += "  \"include\": \"include\",\n";
    s += "  \"deps\": [],\n";
    s += "  \"license\": \"MIT\",\n";
    s += "  \"description\": \"A tiny header-only C++ library.\",\n";
    s += "  \"keywords\": [\n";
    s += "    \"cpp\",\n";
    s += "    \"header-only\",\n";
    s += "    \"vix\"\n";
    s += "  ],\n";
    s += "  \"repository\": \"https://github.com/your-username/" + name + "\",\n";
    s += "  \"authors\": [\n";
    s += "    {\n";
    s += "      \"name\": \"Your Name\",\n";
    s += "      \"github\": \"your-username\"\n";
    s += "    }\n";
    s += "  ]\n";
    s += "}\n";

    return s;
  }

  std::string make_vix_json_app(const std::string &name)
  {
    std::string s;
    s.reserve(4200);

    s += "{\n";
    s += "  \"name\": \"" + name + "\",\n";
    s += "  \"deps\": [],\n";
    s += "  \"vars\": {\n";
    s += "    \"preset\": \"dev-ninja\",\n";
    s += "    \"release_preset\": \"release\",\n";
    s += "    \"log_level\": \"info\"\n";
    s += "  },\n";
    s += "  \"tasks\": {\n";
    s += "    \"fmt\": \"vix fmt\",\n";
    s += "    \"check\": {\n";
    s += "      \"description\": \"Validate project health\",\n";
    s += "      \"command\": \"vix check --preset ${preset} --tests\",\n";
    s += "      \"env\": {\n";
    s += "        \"VIX_LOG_LEVEL\": \"${log_level}\"\n";
    s += "      }\n";
    s += "    },\n";
    s += "    \"test\": {\n";
    s += "      \"description\": \"Run project tests\",\n";
    s += "      \"command\": \"vix tests --preset ${preset} --fail-fast\"\n";
    s += "    },\n";
    s += "    \"dev\": {\n";
    s += "      \"description\": \"Start dev mode\",\n";
    s += "      \"command\": \"vix dev\"\n";
    s += "    },\n";
    s += "    \"ci\": {\n";
    s += "      \"description\": \"Local CI pipeline\",\n";
    s += "      \"deps\": [\n";
    s += "        \"fmt\"\n";
    s += "      ],\n";
    s += "      \"commands\": [\n";
    s += "        \"vix check --preset ${preset} --tests\",\n";
    s += "        \"vix tests --preset ${preset} --fail-fast\"\n";
    s += "      ]\n";
    s += "    },\n";
    s += "    \"release\": {\n";
    s += "      \"description\": \"Release pipeline\",\n";
    s += "      \"deps\": [\n";
    s += "        \"fmt\",\n";
    s += "        \"test\"\n";
    s += "      ],\n";
    s += "      \"vars\": {\n";
    s += "        \"preset\": \"${release_preset}\"\n";
    s += "      },\n";
    s += "      \"env\": {\n";
    s += "        \"VIX_LOG_LEVEL\": \"warn\"\n";
    s += "      },\n";
    s += "      \"cwd\": \"${project_dir}\",\n";
    s += "      \"commands\": [\n";
    s += "        \"vix build --preset ${preset}\",\n";
    s += "        \"vix check --preset ${preset} --tests\"\n";
    s += "      ],\n";
    s += "      \"linux\": {\n";
    s += "        \"commands\": [\n";
    s += "          \"vix build --preset ${preset}\",\n";
    s += "          \"vix check --preset ${preset} --tests --run\"\n";
    s += "        ]\n";
    s += "      },\n";
    s += "      \"windows\": {\n";
    s += "        \"command\": \"vix build --preset dev-msvc\"\n";
    s += "      },\n";
    s += "      \"macos\": {\n";
    s += "        \"commands\": [\n";
    s += "          \"vix build --preset ${preset}\",\n";
    s += "          \"vix tests --preset ${preset}\"\n";
    s += "        ]\n";
    s += "      }\n";
    s += "    },\n";
    s += "    \"package\": {\n";
    s += "      \"description\": \"Build package artifacts\",\n";
    s += "      \"deps\": [\n";
    s += "        \"release\"\n";
    s += "      ],\n";
    s += "      \"commands\": [\n";
    s += "        \"echo Packaging project from ${project_dir}\",\n";
    s += "        \"vix build --preset ${release_preset}\"\n";
    s += "      ]\n";
    s += "    }\n";
    s += "  }\n";
    s += "}\n";

    return s;
  }

  // ------------------------------------------------------------------
  // CMakeLists.txt generators
  // ------------------------------------------------------------------

  std::string make_cmakelists_app(const std::string &projectName, const FeaturesSelection &f)
  {
    std::string s;
    s.reserve(16000);

    s += "cmake_minimum_required(VERSION 3.20)\n";
    s += "project(" + projectName + " LANGUAGES CXX)\n\n";
    s += "set(CMAKE_CXX_STANDARD 20)\n";
    s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    s += "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

    if (f.orm)
      s += "option(VIX_USE_ORM \"Enable Vix ORM (requires vix::orm in install)\" ON)\n";
    if (f.sanitizers)
      s += "option(VIX_ENABLE_SANITIZERS \"Enable ASan/UBSan (dev only)\" ON)\n";
    if (f.static_rt)
      s += "option(VIX_LINK_STATIC \"Static libstdc++/libgcc\" ON)\n";
    if (f.full_static)
      s += "option(VIX_LINK_FULL_STATIC \"Full static link (-static). Prefer musl.\" ON)\n";
    if (f.orm || f.sanitizers || f.static_rt || f.full_static)
      s += "\n";

    s += "# ------------------------------------------------------\n";
    s += "# Core Vix runtime\n";
    s += "# ------------------------------------------------------\n";
    s += "find_package(vix QUIET CONFIG)\n";
    s += "if (NOT vix_FOUND)\n";
    s += "  find_package(Vix CONFIG REQUIRED)\n";
    s += "endif()\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Local registry packages installed with: vix install\n";
    s += "# ------------------------------------------------------\n";
    s += "# If you add packages from the Vix registry, they are wired\n";
    s += "# through .vix/vix_deps.cmake. This file creates the imported\n";
    s += "# targets for local project dependencies.\n";
    s += "#\n";
    s += "# Example:\n";
    s += "#   vix add @cnerium/app\n";
    s += "#   vix install\n";
    s += "#\n";
    s += "# Then uncomment the links you need below.\n";
    s += "if (EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/.vix/vix_deps.cmake\")\n";
    s += "  include(\"${CMAKE_CURRENT_SOURCE_DIR}/.vix/vix_deps.cmake\")\n";
    s += "endif()\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Helpers\n";
    s += "# ------------------------------------------------------\n";
    s += "function(vix_link_optional_targets tgt)\n";
    s += "  foreach(dep IN LISTS ARGN)\n";
    s += "    if (TARGET ${dep})\n";
    s += "      target_link_libraries(${tgt} PRIVATE ${dep})\n";
    s += "    endif()\n";
    s += "  endforeach()\n";
    s += "endfunction()\n\n";

    if (f.static_rt || f.full_static)
    {
      s += "function(vix_apply_static_link_flags tgt)\n";
      s += "  if (MSVC)\n";
      s += "    return()\n";
      s += "  endif()\n";
      if (f.full_static)
      {
        s += "  if (VIX_LINK_FULL_STATIC)\n";
        s += "    target_link_options(${tgt} PRIVATE -static)\n";
        s += "    target_compile_definitions(${tgt} PRIVATE VIX_LINK_FULL_STATIC=1)\n";
        s += "  endif()\n";
      }
      else
      {
        s += "  if (VIX_LINK_STATIC)\n";
        s += "    target_link_options(${tgt} PRIVATE -static-libstdc++ -static-libgcc)\n";
        s += "    target_compile_definitions(${tgt} PRIVATE VIX_LINK_STATIC=1)\n";
        s += "  endif()\n";
      }
      s += "endfunction()\n\n";
    }

    s += "# ------------------------------------------------------\n";
    s += "# Main executable\n";
    s += "# ------------------------------------------------------\n";
    s += "add_executable(" + projectName + " src/main.cpp)\n";
    s += "target_link_libraries(" + projectName + " PRIVATE vix::vix)\n\n";

    s += "# Add local registry libraries here.\n";
    s += "# vix_link_optional_targets(" + projectName + "\n";
    s += "#   cnerium::app\n";
    s += "#   cnerium::http\n";
    s += "#   cnerium::json\n";
    s += "# )\n\n";

    s += "if (MSVC)\n";
    s += "  target_compile_options(" + projectName + " PRIVATE /W4 /permissive-)\n";
    s += "else()\n";
    s += "  target_compile_options(" + projectName + " PRIVATE -Wall -Wextra -Wpedantic)\n";
    s += "endif()\n\n";

    if (f.orm)
    {
      s += "if (VIX_USE_ORM)\n";
      s += "  if (TARGET vix::orm)\n";
      s += "    target_link_libraries(" + projectName + " PRIVATE vix::orm)\n";
      s += "    target_compile_definitions(" + projectName + " PRIVATE VIX_USE_ORM=1)\n";
      s += "  else()\n";
      s += "    message(FATAL_ERROR \"VIX_USE_ORM=ON but vix::orm target is not available in this Vix install\")\n";
      s += "  endif()\n";
      s += "endif()\n\n";
    }

    if (f.static_rt || f.full_static)
      s += "vix_apply_static_link_flags(" + projectName + ")\n\n";

    if (f.sanitizers)
    {
      s += "if (VIX_ENABLE_SANITIZERS AND NOT MSVC)\n";
      s += "  target_compile_options(" + projectName + " PRIVATE -g3 -fno-omit-frame-pointer -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined)\n";
      s += "  target_link_options(" + projectName + " PRIVATE -g -fsanitize=address,undefined)\n";
      s += "  target_compile_definitions(" + projectName + " PRIVATE VIX_SANITIZERS=1 VIX_ASAN=1 VIX_UBSAN=1)\n";
      s += "endif()\n\n";
    }

    s += "# ------------------------------------------------------\n";
    s += "# Tests\n";
    s += "# ------------------------------------------------------\n";
    s += "include(CTest)\n";
    s += "enable_testing()\n\n";

    s += "add_executable(" + projectName + "_basic_test tests/test_basic.cpp)\n";
    s += "target_link_libraries(" + projectName + "_basic_test PRIVATE vix::vix)\n\n";

    s += "# vix_link_optional_targets(" + projectName + "_basic_test\n";
    s += "#   cnerium::app\n";
    s += "# )\n\n";

    s += "if (MSVC)\n";
    s += "  target_compile_options(" + projectName + "_basic_test PRIVATE /W4 /permissive-)\n";
    s += "else()\n";
    s += "  target_compile_options(" + projectName + "_basic_test PRIVATE -Wall -Wextra -Wpedantic)\n";
    s += "endif()\n\n";

    if (f.static_rt || f.full_static)
      s += "vix_apply_static_link_flags(" + projectName + "_basic_test)\n\n";

    s += "add_test(NAME " + projectName + ".basic COMMAND " + projectName + "_basic_test)\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Convenience target\n";
    s += "# ------------------------------------------------------\n";
    s += "add_custom_target(run\n";
    s += "  COMMAND $<TARGET_FILE:" + projectName + ">\n";
    s += "  DEPENDS " + projectName + "\n";
    s += "  USES_TERMINAL\n";
    s += ")\n";

    return s;
  }

  std::string make_cmakelists_lib(const std::string &name)
  {
    std::string s;
    s.reserve(16000);

    s += "cmake_minimum_required(VERSION 3.20)\n";
    s += "project(" + name + " LANGUAGES CXX)\n\n";
    s += "set(CMAKE_CXX_STANDARD 20)\n";
    s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    s += "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Options\n";
    s += "# ------------------------------------------------------\n";
    s += "option(" + name + "_BUILD_TESTS \"Build tests\" OFF)\n";
    s += "option(" + name + "_BUILD_EXAMPLES \"Build examples\" OFF)\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Library\n";
    s += "# ------------------------------------------------------\n";
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

    s += "# ------------------------------------------------------\n";
    s += "# Helpers\n";
    s += "# ------------------------------------------------------\n";
    s += "function(" + name + "_add_example target file)\n";
    s += "  add_executable(${target} ${file})\n";
    s += "  target_link_libraries(${target} PRIVATE " + name + "::" + name + ")\n\n";
    s += "  if (MSVC)\n";
    s += "    target_compile_options(${target} PRIVATE /W4 /permissive-)\n";
    s += "  else()\n";
    s += "    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)\n";
    s += "  endif()\n";
    s += "endfunction()\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Examples\n";
    s += "# ------------------------------------------------------\n";
    s += "if (" + name + "_BUILD_EXAMPLES)\n";
    s += "  if (EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt\")\n";
    s += "    add_subdirectory(examples)\n";
    s += "  endif()\n";
    s += "endif()\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Tests\n";
    s += "# ------------------------------------------------------\n";
    s += "if (" + name + "_BUILD_TESTS)\n";
    s += "  include(CTest)\n";
    s += "  enable_testing()\n\n";

    s += "  find_package(vix QUIET CONFIG)\n";
    s += "  if (NOT vix_FOUND)\n";
    s += "    find_package(Vix CONFIG REQUIRED)\n";
    s += "  endif()\n\n";

    s += "  add_executable(" + name + "_test_basic tests/test_basic.cpp)\n";
    s += "  target_link_libraries(" + name + "_test_basic PRIVATE\n";
    s += "    " + name + "::" + name + "\n";
    s += "    vix::tests\n";
    s += "  )\n\n";

    s += "  if (MSVC)\n";
    s += "    target_compile_options(" + name + "_test_basic PRIVATE /W4 /permissive-)\n";
    s += "  else()\n";
    s += "    target_compile_options(" + name + "_test_basic PRIVATE -Wall -Wextra -Wpedantic)\n";
    s += "  endif()\n\n";

    s += "  add_test(NAME " + name + ".basic COMMAND " + name + "_test_basic)\n";
    s += "endif()\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
