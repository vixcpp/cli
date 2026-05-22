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
    readme.reserve(7000);

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
    readme += "```txt\n";
    readme += "http://localhost:8080\n";
    readme += "```\n\n";

    readme += "## Project manifest\n\n";
    readme += "This project uses `vix.app` as its build manifest.\n\n";
    readme += "`vix.app` describes the application target, sources, include directories, ";
    readme += "compiler options, link options, packages, resources and output directory.\n\n";

    readme += "Vix generates the internal CMake project automatically under `.vix/generated/app/`.\n";
    readme += "You do not need to write a `CMakeLists.txt` for a simple app.\n\n";

    readme += "## Dependencies\n\n";
    readme += "This project also uses `vix.json` for package metadata, tasks and dependencies.\n\n";
    readme += "Workflow:\n\n";
    readme += "- `vix add <pkg>` adds a dependency\n";
    readme += "- `vix install` installs dependencies\n";
    readme += "- `vix.lock` keeps installs reproducible\n\n";

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
    readme += "- `server.port` maps to `SERVER_PORT`\n";
    readme += "- `database.default.host` maps to `DATABASE_DEFAULT_HOST`\n";
    readme += "- `database.default.name` maps to `DATABASE_DEFAULT_NAME`\n\n";

    readme += "This keeps the C++ API clean and environment-driven.\n\n";

    readme += "## Environment layers\n\n";
    readme += "You can use multiple env files:\n\n";
    readme += "- `.env`\n";
    readme += "- `.env.local`\n";
    readme += "- `.env.production`\n\n";
    readme += "Use `.env` for development and environment-specific files for deployment.\n";

    return readme;
  }

  std::string make_readme_vue_app(const std::string &projectName)
  {
    std::string readme;
    readme.reserve(3000);

    readme += "# " + projectName + "\n\n";
    readme += "Vue frontend + Vix C++ backend.\n\n";

    readme += "## Quick start\n\n";
    readme += "Install frontend dependencies:\n\n";
    readme += "```bash\n";
    readme += "cd frontend\n";
    readme += "npm install\n";
    readme += "cd ..\n";
    readme += "```\n\n";

    readme += "Start the Vix backend:\n\n";
    readme += "```bash\n";
    readme += "vix run\n";
    readme += "```\n\n";

    readme += "Start the Vue frontend:\n\n";
    readme += "```bash\n";
    readme += "cd frontend\n";
    readme += "npm run dev\n";
    readme += "```\n\n";

    readme += "Then open the Vue dev server shown by Vite.\n\n";

    readme += "## Project layout\n\n";
    readme += "```txt\n";
    readme += "src/main.cpp        Vix C++ backend\n";
    readme += "frontend/           Vue frontend\n";
    readme += "frontend/src/       Vue source files\n";
    readme += "vix.app             Vix application manifest\n";
    readme += "vix.json            Vix project metadata and tasks\n";
    readme += "```\n\n";

    readme += "## API\n\n";
    readme += "The Vue frontend calls the Vix backend through `/api/*`.\n\n";
    readme += "During development, Vite proxies `/api` to:\n\n";
    readme += "```txt\n";
    readme += "http://localhost:8080\n";
    readme += "```\n\n";

    readme += "## Build\n\n";
    readme += "Build the Vix backend:\n\n";
    readme += "```bash\n";
    readme += "vix build\n";
    readme += "```\n\n";

    readme += "Build the Vue frontend:\n\n";
    readme += "```bash\n";
    readme += "cd frontend\n";
    readme += "npm run build\n";
    readme += "```\n\n";

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

    readme += "Build tests and examples:\n\n";
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
    readme += "- Uses embedded Vix CMake presets for library builds\n";
    readme += "- Automatically configures and builds with Vix\n";
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

    json += ",\n";
    json += "    { \"name\": \"build-release\", \"displayName\": \"Build (ALL, Ninja Release)\", \"configurePreset\": \"release\" },\n";
    json += "    { \"name\": \"build-msvc\", \"displayName\": \"Build (ALL, MSVC)\", \"configurePreset\": \"dev-msvc\", \"configuration\": \"Release\" }\n";
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
  // vix.app generators
  // ------------------------------------------------------------------

  std::string make_project_manifest_app(const std::string &name, const FeaturesSelection &f)
  {
    std::string s;
    s.reserve(1600);

    s += "# Vix application manifest\n";
    s += "# This file is read by Vix and converted to an internal CMake project.\n";
    s += "# The generated CMakeLists.txt lives under .vix/generated/app/.\n\n";

    s += "name = \"" + name + "\"\n";
    s += "type = \"executable\"\n";
    s += "standard = \"c++20\"\n\n";

    s += "sources = [\n";
    s += "  \"src/main.cpp\",\n";
    s += "]\n\n";

    s += "include_dirs = [\n";
    s += "  \"src\",\n";
    s += "]\n\n";

    s += "defines = [\n";

    if (f.orm)
      s += "  \"VIX_USE_ORM=1\",\n";
    if (f.sanitizers)
      s += "  \"VIX_SANITIZERS=1\",\n";
    if (f.static_rt)
      s += "  \"VIX_LINK_STATIC=1\",\n";
    if (f.full_static)
      s += "  \"VIX_LINK_FULL_STATIC=1\",\n";

    s += "]\n\n";

    s += "compile_options = [\n";
    s += "  \"$<$<CXX_COMPILER_ID:MSVC>:/W4>\",\n";
    s += "  \"$<$<CXX_COMPILER_ID:MSVC>:/permissive->\",\n";
    s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>\",\n";
    s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>\",\n";
    s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wpedantic>\",\n";

    if (f.sanitizers)
    {
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-g3>\",\n";
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-omit-frame-pointer>\",\n";
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O1>\",\n";
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=address,undefined>\",\n";
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-sanitize-recover=undefined>\",\n";
    }

    s += "]\n\n";

    s += "link_options = [\n";

    if (f.sanitizers)
    {
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-g>\",\n";
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=address,undefined>\",\n";
    }

    if (f.full_static)
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-static>\",\n";
    else if (f.static_rt)
    {
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-static-libstdc++>\",\n";
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-static-libgcc>\",\n";
    }

    s += "]\n\n";

    s += "compile_features = [\n";
    s += "  \"cxx_std_20\",\n";
    s += "]\n\n";

    s += "packages = [\n";
    s += "  \"vix\",\n";
    s += "]\n\n";

    s += "links = [\n";
    s += "  \"vix::vix\",\n";

    if (f.orm)
      s += "  \"vix::orm\",\n";

    s += "]\n\n";

    s += "resources = [\n";
    s += "  \".env=.env\",\n";
    s += "]\n\n";

    s += "output_dir = \"bin\"\n";

    return s;
  }

  std::string make_project_manifest_lib(const std::string &name)
  {
    std::string s;
    s.reserve(1200);

    s += "version = 1\n\n";
    s += "[app]\n";
    s += "kind = \"project\"\n";
    s += "dir = \".\"\n";
    s += "name = \"" + name + "\"\n";
    s += "entry = \"tests/test_basic.cpp\"\n\n";
    s += "[build]\n";
    s += "preset = \"dev-ninja\"\n";

    return s;
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

  std::string make_vix_json_vue_app(const std::string &name)
  {
    std::string s;
    s.reserve(5200);

    s += "{\n";
    s += "  \"name\": \"" + name + "\",\n";
    s += "  \"type\": \"app\",\n";
    s += "  \"template\": \"vue\",\n";
    s += "  \"deps\": [],\n";
    s += "  \"frontend\": {\n";
    s += "    \"framework\": \"vue\",\n";
    s += "    \"dir\": \"frontend\",\n";
    s += "    \"dev\": \"npm run dev\",\n";
    s += "    \"build\": \"npm run build\",\n";
    s += "    \"dist\": \"frontend/dist\"\n";
    s += "  },\n";
    s += "  \"vars\": {\n";
    s += "    \"preset\": \"dev-ninja\",\n";
    s += "    \"release_preset\": \"release\",\n";
    s += "    \"log_level\": \"info\"\n";
    s += "  },\n";
    s += "  \"tasks\": {\n";
    s += "    \"frontend:install\": {\n";
    s += "      \"description\": \"Install Vue dependencies\",\n";
    s += "      \"command\": \"npm install\",\n";
    s += "      \"cwd\": \"frontend\"\n";
    s += "    },\n";
    s += "    \"frontend:dev\": {\n";
    s += "      \"description\": \"Start Vue dev server\",\n";
    s += "      \"command\": \"npm run dev\",\n";
    s += "      \"cwd\": \"frontend\"\n";
    s += "    },\n";
    s += "    \"frontend:build\": {\n";
    s += "      \"description\": \"Build Vue frontend\",\n";
    s += "      \"command\": \"npm run build\",\n";
    s += "      \"cwd\": \"frontend\"\n";
    s += "    },\n";
    s += "    \"backend:dev\": {\n";
    s += "      \"description\": \"Start Vix backend\",\n";
    s += "      \"command\": \"vix run\"\n";
    s += "    },\n";
    s += "    \"backend:build\": {\n";
    s += "      \"description\": \"Build Vix backend\",\n";
    s += "      \"command\": \"vix build --preset ${preset}\"\n";
    s += "    },\n";
    s += "    \"fmt\": \"vix fmt\",\n";
    s += "    \"check\": {\n";
    s += "      \"description\": \"Validate backend project health\",\n";
    s += "      \"command\": \"vix check --preset ${preset} --tests\",\n";
    s += "      \"env\": {\n";
    s += "        \"VIX_LOG_LEVEL\": \"${log_level}\"\n";
    s += "      }\n";
    s += "    },\n";
    s += "    \"test\": {\n";
    s += "      \"description\": \"Run backend tests\",\n";
    s += "      \"command\": \"vix tests --preset ${preset} --fail-fast\"\n";
    s += "    },\n";
    s += "    \"ci\": {\n";
    s += "      \"description\": \"Local CI pipeline\",\n";
    s += "      \"commands\": [\n";
    s += "        \"vix check --preset ${preset} --tests\",\n";
    s += "        \"vix tests --preset ${preset} --fail-fast\",\n";
    s += "        \"cd frontend && npm install\",\n";
    s += "        \"cd frontend && npm run build\"\n";
    s += "      ]\n";
    s += "    }\n";
    s += "}\n";

    return s;
  }

  std::string make_vue_package_json(const std::string &projectName)
  {
    std::string s;
    s.reserve(800);

    s += "{\n";
    s += "  \"name\": \"" + projectName + "-frontend\",\n";
    s += "  \"private\": true,\n";
    s += "  \"version\": \"0.1.0\",\n";
    s += "  \"type\": \"module\",\n";
    s += "  \"scripts\": {\n";
    s += "    \"dev\": \"vite\",\n";
    s += "    \"build\": \"vite build\",\n";
    s += "    \"preview\": \"vite preview\"\n";
    s += "  },\n";
    s += "  \"dependencies\": {\n";
    s += "    \"@vitejs/plugin-vue\": \"latest\",\n";
    s += "    \"vite\": \"latest\",\n";
    s += "    \"vue\": \"latest\"\n";
    s += "  },\n";
    s += "  \"devDependencies\": {}\n";
    s += "}\n";

    return s;
  }

  std::string make_vue_index_html(const std::string &projectName)
  {
    std::string s;
    s.reserve(600);

    s += "<!doctype html>\n";
    s += "<html lang=\"en\">\n";
    s += "  <head>\n";
    s += "    <meta charset=\"UTF-8\" />\n";
    s += "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\n";
    s += "    <title>" + projectName + "</title>\n";
    s += "  </head>\n";
    s += "  <body>\n";
    s += "    <div id=\"app\"></div>\n";
    s += "    <script type=\"module\" src=\"/src/main.js\"></script>\n";
    s += "  </body>\n";
    s += "</html>\n";

    return s;
  }

  std::string make_vue_vite_config()
  {
    return R"JS(import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";

export default defineConfig({
  clearScreen: false,
  plugins: [vue()],
  server: {
    host: "0.0.0.0",
    proxy: {
      "/api": "http://localhost:8080"
    }
  }
});
)JS";
  }

  std::string make_vue_main_js()
  {
    return R"JS(import { createApp } from "vue";
import App from "./App.vue";

createApp(App).mount("#app");
)JS";
  }

  std::string make_vue_app_vue()
  {
    return R"VUE(<script setup>
import { ref } from "vue";

const message = ref("Loading from Vix...");

async function loadMessage() {
  try {
    const response = await fetch("/api/hello");
    const data = await response.json();
    message.value = data.message || "Hello from Vix";
  } catch (error) {
    message.value = "Could not reach the Vix backend";
  }
}

loadMessage();
</script>

<template>
  <main class="page">
    <section class="card">
      <p class="eyebrow">Vue + Vix</p>
      <h1>Frontend powered by Vue</h1>
      <p class="message">{{ message }}</p>
    </section>
  </main>
</template>

<style scoped>
.page {
  min-height: 100vh;
  display: grid;
  place-items: center;
  background: #0f172a;
  color: #f8fafc;
  font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
}

.card {
  width: min(92vw, 560px);
  padding: 48px;
  border-radius: 28px;
  background: rgba(15, 23, 42, 0.9);
  border: 1px solid rgba(148, 163, 184, 0.24);
  box-shadow: 0 24px 80px rgba(0, 0, 0, 0.35);
}

.eyebrow {
  margin: 0 0 12px;
  color: #38bdf8;
  font-weight: 700;
  letter-spacing: 0.12em;
  text-transform: uppercase;
}

h1 {
  margin: 0;
  font-size: clamp(2rem, 6vw, 4rem);
  line-height: 1;
}

.message {
  margin-top: 24px;
  color: #cbd5e1;
  font-size: 1.1rem;
}
</style>
)VUE";
  }

  // ------------------------------------------------------------------
  // CMakeLists.txt generators
  // ------------------------------------------------------------------

  std::string make_cmakelists_app(const std::string &projectName, const FeaturesSelection &)
  {
    std::string s;
    s.reserve(800);

    s += "# This file is intentionally minimal.\n";
    s += "# New Vix applications should use vix.app instead of a handwritten CMakeLists.txt.\n";
    s += "# Project: " + projectName + "\n\n";
    s += "cmake_minimum_required(VERSION 3.24)\n";
    s += "project(" + projectName + " LANGUAGES CXX)\n\n";
    s += "message(FATAL_ERROR \"This app was generated for vix.app. Run Vix from the project root and remove this fallback CMakeLists.txt if it was generated by mistake.\")\n";

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

  // ------------------------------------------------------------------
  // Game generators
  // ------------------------------------------------------------------

  std::string make_game_main_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(2600);

    s += "#include <vix/game/all.hpp>\n";
    s += "#include <vix/print.hpp>\n\n";

    s += "class MainScene final : public vix::game::Scene\n";
    s += "{\n";
    s += "public:\n";
    s += "  vix::game::GameBoolResult on_load() override\n";
    s += "  {\n";
    s += "    vix::print(\"Main scene loaded\");\n";
    s += "    return vix::game::Scene::on_load();\n";
    s += "  }\n\n";

    s += "  void on_update(const vix::game::Frame &frame) override\n";
    s += "  {\n";
    s += "    vix::print(\"frame:\", frame.index);\n\n";
    s += "    if (frame.index >= 5)\n";
    s += "    {\n";
    s += "      app().stop();\n";
    s += "    }\n";
    s += "  }\n";
    s += "};\n\n";

    s += "int main()\n";
    s += "{\n";
    s += "  vix::game::App app;\n";
    s += "  app.set_title(\"" + projectName + "\");\n\n";

    s += "  vix::game::GameRuntime runtime(app);\n\n";

    s += "  auto runtime_init = runtime.init();\n";
    s += "  if (!runtime_init)\n";
    s += "  {\n";
    s += "    vix::print(\"runtime init failed:\", runtime_init.error().message());\n";
    s += "    return 1;\n";
    s += "  }\n\n";

    s += "  auto scene = app.scenes().create<MainScene>(\"main\");\n";
    s += "  if (!scene)\n";
    s += "  {\n";
    s += "    vix::print(\"scene creation failed:\", scene.error().message());\n";
    s += "    return 1;\n";
    s += "  }\n\n";

    s += "  auto active = app.scenes().set_active(\"main\");\n";
    s += "  if (!active)\n";
    s += "  {\n";
    s += "    vix::print(\"scene activation failed:\", active.error().message());\n";
    s += "    return 1;\n";
    s += "  }\n\n";

    s += "  auto result = app.run();\n";
    s += "  if (!result)\n";
    s += "  {\n";
    s += "    vix::print(\"game failed:\", result.error().message());\n";
    s += "    return 1;\n";
    s += "  }\n\n";

    s += "  return 0;\n";
    s += "}\n";

    return s;
  }

  std::string make_game_package_json(const std::string &projectName)
  {
    std::string s;
    s.reserve(800);

    s += "{\n";
    s += "  \"name\": \"" + projectName + "\",\n";
    s += "  \"version\": \"0.1.0\",\n";
    s += "  \"author\": \"\",\n";
    s += "  \"entry_scene\": \"main\",\n";
    s += "  \"asset_root\": \"assets\",\n";
    s += "  \"output_dir\": \"dist\",\n";
    s += "  \"scenes\": [\n";
    s += "    \"main\"\n";
    s += "  ],\n";
    s += "  \"assets\": []\n";
    s += "}\n";

    return s;
  }

  std::string make_project_manifest_game(const std::string &projectName)
  {
    std::string s;
    s.reserve(1400);

    s += "# Vix game manifest\n";
    s += "# This file is read by Vix and converted to an internal CMake project.\n\n";

    s += "name = \"" + projectName + "\"\n";
    s += "type = \"executable\"\n";
    s += "standard = \"c++20\"\n\n";

    s += "sources = [\n";
    s += "  \"src/main.cpp\",\n";
    s += "]\n\n";

    s += "include_dirs = [\n";
    s += "  \"src\",\n";
    s += "]\n\n";

    s += "compile_features = [\n";
    s += "  \"cxx_std_20\",\n";
    s += "]\n\n";

    s += "packages = [\n";
    s += "  \"vix\",\n";
    s += "]\n\n";

    s += "links = [\n";
    s += "  \"vix::game\",\n";
    s += "  \"vix::io\",\n";
    s += "]\n\n";

    s += "resources = [\n";
    s += "  \"assets=assets\",\n";
    s += "  \"game.package.json=game.package.json\",\n";
    s += "]\n\n";

    s += "output_dir = \"bin\"\n";

    return s;
  }

  std::string make_vix_json_game(const std::string &projectName)
  {
    std::string s;
    s.reserve(1800);

    s += "{\n";
    s += "  \"name\": \"" + projectName + "\",\n";
    s += "  \"deps\": [],\n";
    s += "  \"vars\": {\n";
    s += "    \"preset\": \"dev-ninja\",\n";
    s += "    \"log_level\": \"info\"\n";
    s += "  },\n";
    s += "  \"tasks\": {\n";
    s += "    \"dev\": \"vix run\",\n";
    s += "    \"build\": \"vix build\",\n";
    s += "    \"run\": \"vix run\",\n";
    s += "    \"export\": \"vix run && vix build\",\n";
    s += "    \"check\": {\n";
    s += "      \"description\": \"Build and validate the game project\",\n";
    s += "      \"command\": \"vix build\"\n";
    s += "    }\n";
    s += "  }\n";
    s += "}\n";

    return s;
  }

  std::string make_readme_game(const std::string &projectName)
  {
    std::string readme;
    readme.reserve(4200);

    readme += "# " + projectName + "\n\n";
    readme += "Vix game project generated by `vix new --game`.\n\n";

    readme += "This project uses the Vix game foundation:\n\n";
    readme += "- `App`\n";
    readme += "- `GameRuntime`\n";
    readme += "- `Scene`\n";
    readme += "- `SceneManager`\n";
    readme += "- `GamePackage`\n";
    readme += "- `vix.app`\n";
    readme += "- `game.package.json`\n\n";

    readme += "## Quick start\n\n";
    readme += "```bash\n";
    readme += "cd " + projectName + "\n";
    readme += "vix build\n";
    readme += "vix run\n";
    readme += "```\n\n";

    readme += "Expected output:\n\n";
    readme += "```text\n";
    readme += "Main scene loaded\n";
    readme += "frame: 0\n";
    readme += "frame: 1\n";
    readme += "frame: 2\n";
    readme += "frame: 3\n";
    readme += "frame: 4\n";
    readme += "frame: 5\n";
    readme += "```\n\n";

    readme += "## Project layout\n\n";
    readme += "```text\n";
    readme += "assets/             Game assets\n";
    readme += "game.package.json   Game package metadata\n";
    readme += "README.md           Project documentation\n";
    readme += "src/main.cpp        Game entry point\n";
    readme += "vix.app             Vix application manifest\n";
    readme += "vix.json            Vix project metadata and tasks\n";
    readme += "```\n\n";

    readme += "## Game package\n\n";
    readme += "`game.package.json` describes the game project.\n\n";
    readme += "It contains:\n\n";
    readme += "- game name\n";
    readme += "- version\n";
    readme += "- entry scene\n";
    readme += "- asset root\n";
    readme += "- output directory\n";
    readme += "- scene list\n";
    readme += "- asset list\n\n";

    readme += "## Assets\n\n";
    readme += "Put your game assets in:\n\n";
    readme += "```text\n";
    readme += "assets/\n";
    readme += "```\n\n";

    readme += "The Vix game export pipeline can scan this directory and write exported asset metadata into `export.json`.\n\n";

    readme += "## Build manifest\n\n";
    readme += "This project uses `vix.app` as its application manifest.\n\n";
    readme += "The manifest links:\n\n";
    readme += "- `vix::game`\n";
    readme += "- `vix::io`\n\n";

    readme += "It also declares resources:\n\n";
    readme += "- `assets=assets`\n";
    readme += "- `game.package.json=game.package.json`\n\n";

    return readme;
  }
} // namespace vix::commands::new_cmd::templates
