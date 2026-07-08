/**
 * @file AppTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/AppTemplates.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{
  std::string make_main_cpp_app(const std::string &projectName)
  {
    std::string s;
    s.reserve(2400);

    s += "/**\n";
    s += " * @file main.cpp\n";
    s += " * @brief Entry point for the " + projectName + " Vix application.\n";
    s += " */\n\n";

    s += "#include <app/ModuleRegistry.hpp>\n\n";
    s += "#include <vix_app_modules.hpp>\n\n";

    s += "#include <vix.hpp>\n";
    s += "#include <vix/executor/RuntimeExecutor.hpp>\n\n";

    s += "#include <memory>\n\n";

    s += "int main()\n";
    s += "{\n";
    s += "  vix::config::Config cfg{\".env\"};\n\n";

    s += "  auto executor = std::make_shared<vix::executor::RuntimeExecutor>(1u);\n";
    s += "  vix::App app{executor};\n\n";

    s += "  app.get(\"/\", [](vix::Request &req, vix::Response &res)\n";
    s += "  {\n";
    s += "    (void)req;\n";
    s += "    res.send(\"Hello from " + projectName + "\");\n";
    s += "  });\n\n";

    s += "  app::ModuleRegistry::register_all(app);\n\n";

    s += "  return vix::app_generated::run_app(app, cfg, executor);\n";
    s += "}\n";

    return s;
  }
  std::string make_app_module_registry_hpp()
  {
    std::string s;
    s.reserve(900);

    s += "/**\n";
    s += " * @file ModuleRegistry.hpp\n";
    s += " * @brief Central module registry for a Vix application.\n";
    s += " */\n\n";

    s += "#ifndef VIX_GENERATED_APP_MODULE_REGISTRY_HPP\n";
    s += "#define VIX_GENERATED_APP_MODULE_REGISTRY_HPP\n\n";

    s += "namespace vix\n";
    s += "{\n";
    s += "  class App;\n";
    s += "}\n\n";

    s += "namespace app\n";
    s += "{\n";
    s += "  class ModuleRegistry\n";
    s += "  {\n";
    s += "  public:\n";
    s += "    static void register_all(vix::App &app);\n";
    s += "  };\n";
    s += "} // namespace app\n\n";

    s += "#endif // VIX_GENERATED_APP_MODULE_REGISTRY_HPP\n";

    return s;
  }

  std::string make_app_module_registry_cpp()
  {
    std::string s;
    s.reserve(1200);

    s += "/**\n";
    s += " * @file ModuleRegistry.cpp\n";
    s += " * @brief Application module registration.\n";
    s += " */\n\n";

    s += "#include <app/ModuleRegistry.hpp>\n\n";
    s += "#include <vix_app_modules.hpp>\n\n";

    s += "namespace app\n";
    s += "{\n";
    s += "  void ModuleRegistry::register_all(vix::App &app)\n";
    s += "  {\n";
    s += "    vix::app_generated::register_app_modules(app);\n";
    s += "  }\n";
    s += "} // namespace app\n";

    return s;
  }

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

    readme += "## Modules\n\n";
    readme += "This project can also be split into internal Vix modules when the application starts growing.\n\n";

    readme += "Start module mode with:\n\n";
    readme += "```bash\n";
    readme += "vix modules init\n";
    readme += "```\n\n";

    readme += "Create a module:\n\n";
    readme += "```bash\n";
    readme += "vix modules add products\n";
    readme += "```\n\n";

    readme += "Vix creates a module folder with its own source files, public header, `vix.module`, and an example test file.\n\n";

    readme += "List modules with:\n\n";
    readme += "```bash\n";
    readme += "vix modules list\n";
    readme += "```\n\n";

    readme += "Enable or disable a module from the main application manifest:\n\n";
    readme += "```bash\n";
    readme += "vix modules enable products\n";
    readme += "vix modules disable products\n";
    readme += "```\n\n";

    readme += "The main `vix.app` file decides which modules are active. A disabled module can stay on disk without being compiled into the application.\n\n";

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
    s += "  \"src/app/ModuleRegistry.cpp\",\n";
    s += "]\n\n";

    s += "include_dirs = [\n";
    s += "  \"include\",\n";
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

} // namespace vix::commands::new_cmd::templates
