/**
 * @file LibTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/LibTemplates.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{

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
