/**
 * @file BackendTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/BackendTemplates.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{

  namespace
  {
    std::string make_backend_namespace_open(const std::string &projectName, const std::string &area)
    {
      return "namespace " + projectName + "::" + area + "\n{\n";
    }

    std::string make_backend_namespace_close(const std::string &projectName, const std::string &area)
    {
      return "} // namespace " + projectName + "::" + area + "\n";
    }
  } // namespace

  std::string make_backend_main_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(400);

    s += "#include <" + projectName + "/app/AppBootstrap.hpp>\n\n";
    s += "int main()\n";
    s += "{\n";
    s += "  " + projectName + "::app::AppBootstrap bootstrap;\n";
    s += "  return bootstrap.run();\n";
    s += "}\n";

    return s;
  }

  std::string make_backend_app_bootstrap_hpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(900);

    s += "#pragma once\n\n";
    s += "namespace " + projectName + "::app\n";
    s += "{\n";
    s += "  class AppBootstrap\n";
    s += "  {\n";
    s += "  public:\n";
    s += "    AppBootstrap() = default;\n";
    s += "    ~AppBootstrap() = default;\n\n";
    s += "    AppBootstrap(const AppBootstrap &) = delete;\n";
    s += "    AppBootstrap &operator=(const AppBootstrap &) = delete;\n";
    s += "    AppBootstrap(AppBootstrap &&) = delete;\n";
    s += "    AppBootstrap &operator=(AppBootstrap &&) = delete;\n\n";
    s += "    int run();\n";
    s += "  };\n";
    s += "} // namespace " + projectName + "::app\n";

    return s;
  }

  std::string make_backend_app_bootstrap_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(1800);

    s += "#include <" + projectName + "/app/AppBootstrap.hpp>\n";
    s += "#include <" + projectName + "/presentation/middleware/MiddlewareRegistry.hpp>\n";
    s += "#include <" + projectName + "/presentation/routes/RouteRegistry.hpp>\n\n";
    s += "#include <vix.hpp>\n\n";
    s += "namespace " + projectName + "::app\n";
    s += "{\n";
    s += "  int AppBootstrap::run()\n";
    s += "  {\n";
    s += "    vix::App app;\n\n";
    s += "    presentation::middleware::MiddlewareRegistry::register_all(app);\n";
    s += "    presentation::routes::RouteRegistry::register_all(app);\n\n";
    s += "    app.run(8080);\n";
    s += "    return 0;\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::app\n";

    return s;
  }

  std::string make_backend_route_registry_hpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(800);

    s += "#pragma once\n\n";
    s += "namespace vix\n";
    s += "{\n";
    s += "  class App;\n";
    s += "}\n\n";
    s += "namespace " + projectName + "::presentation::routes\n";
    s += "{\n";
    s += "  class RouteRegistry\n";
    s += "  {\n";
    s += "  public:\n";
    s += "    static void register_all(vix::App &app);\n";
    s += "  };\n";
    s += "} // namespace " + projectName + "::presentation::routes\n";

    return s;
  }

  std::string make_backend_route_registry_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(1200);

    s += "#include <" + projectName + "/presentation/routes/RouteRegistry.hpp>\n";
    s += "#include <" + projectName + "/presentation/controllers/HealthController.hpp>\n\n";
    s += "#include <vix.hpp>\n\n";
    s += "namespace " + projectName + "::presentation::routes\n";
    s += "{\n";
    s += "  void RouteRegistry::register_all(vix::App &app)\n";
    s += "  {\n";
    s += "    controllers::HealthController::register_routes(app);\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::presentation::routes\n";

    return s;
  }

  std::string make_backend_middleware_registry_hpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(800);

    s += "#pragma once\n\n";
    s += "namespace vix\n";
    s += "{\n";
    s += "  class App;\n";
    s += "}\n\n";
    s += "namespace " + projectName + "::presentation::middleware\n";
    s += "{\n";
    s += "  class MiddlewareRegistry\n";
    s += "  {\n";
    s += "  public:\n";
    s += "    static void register_all(vix::App &app);\n";
    s += "  };\n";
    s += "} // namespace " + projectName + "::presentation::middleware\n";

    return s;
  }

  std::string make_backend_middleware_registry_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(900);

    s += "#include <" + projectName + "/presentation/middleware/MiddlewareRegistry.hpp>\n\n";
    s += "#include <vix.hpp>\n\n";
    s += "namespace " + projectName + "::presentation::middleware\n";
    s += "{\n";
    s += "  void MiddlewareRegistry::register_all(vix::App & /*app*/)\n";
    s += "  {\n";
    s += "    // Register production middleware here.\n";
    s += "    // Examples: logging, request id, CORS, authentication, rate limiting.\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::presentation::middleware\n";

    return s;
  }

  std::string make_backend_health_controller_hpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(800);

    s += "#pragma once\n\n";
    s += "namespace vix\n";
    s += "{\n";
    s += "  class App;\n";
    s += "}\n\n";
    s += "namespace " + projectName + "::presentation::controllers\n";
    s += "{\n";
    s += "  class HealthController\n";
    s += "  {\n";
    s += "  public:\n";
    s += "    static void register_routes(vix::App &app);\n";
    s += "  };\n";
    s += "} // namespace " + projectName + "::presentation::controllers\n";

    return s;
  }

  std::string make_backend_health_controller_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(1300);

    s += "#include <" + projectName + "/presentation/controllers/HealthController.hpp>\n\n";
    s += "#include <vix.hpp>\n\n";
    s += "namespace " + projectName + "::presentation::controllers\n";
    s += "{\n";
    s += "  void HealthController::register_routes(vix::App &app)\n";
    s += "  {\n";
    s += "    app.get(\"/health\", [](vix::Request &, vix::Response &res) {\n";
    s += "      res.send(R\"JSON({\"status\":\"ok\"})JSON\");\n";
    s += "    });\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::presentation::controllers\n";

    return s;
  }

  std::string make_backend_production_config_json()
  {
    return R"JSON({
  "app": {
    "env": "production"
  },
  "server": {
    "host": "0.0.0.0",
    "port": 8080,
    "request_timeout": 5000,
    "io_threads": 0,
    "session_timeout_sec": 20
  },
  "logging": {
    "level": "info",
    "async": true,
    "queue_max": 20000,
    "drop_on_overflow": true
  },
  "storage": {
    "path": "storage"
  },
  "health": {
    "path": "/health"
  }
}
)JSON";
  }

  std::string make_backend_env_example()
  {
    return R"(# ----------------------------------
# Server
# ----------------------------------
SERVER_HOST=0.0.0.0
SERVER_PORT=8080
APP_ENV=development

# ----------------------------------
# Logging
# ----------------------------------
LOG_LEVEL=info
LOGGING_ASYNC=true

# ----------------------------------
# Storage
# ----------------------------------
STORAGE_PATH=storage

# ----------------------------------
# Database
# ----------------------------------
DATABASE_ENGINE=mysql
DATABASE_DEFAULT_HOST=127.0.0.1
DATABASE_DEFAULT_PORT=3306
DATABASE_DEFAULT_USER=root
DATABASE_DEFAULT_PASSWORD=
DATABASE_DEFAULT_NAME=appdb
)";
  }

  std::string make_backend_basic_test_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(1000);

    s += "#include <vix/tests/tests.hpp>\n\n";
    s += "int main()\n";
    s += "{\n";
    s += "  using namespace vix::tests;\n\n";
    s += "  auto &registry = TestRegistry::instance();\n";
    s += "  registry.clear();\n\n";
    s += "  registry.add(TestCase(\"" + projectName + " backend basic test\", [] {\n";
    s += "    Assert::equal(2 + 2, 4);\n";
    s += "  }));\n\n";
    s += "  return TestRunner::run_all_and_exit();\n";
    s += "}\n";

    return s;
  }

  std::string make_readme_backend(const std::string &projectName)
  {
    std::string readme;
    readme.reserve(5000);

    readme += "# " + projectName + "\n\n";
    readme += "Production-oriented Vix backend application.\n\n";

    readme += "## Quick start\n\n";
    readme += "```bash\n";
    readme += "cd " + projectName + "\n";
    readme += "cp .env.example .env\n";
    readme += "vix build\n";
    readme += "vix run\n";
    readme += "```\n\n";

    readme += "Health check:\n\n";
    readme += "```bash\n";
    readme += "curl http://localhost:8080/health\n";
    readme += "```\n\n";

    readme += "## Project layout\n\n";
    readme += "```txt\n";
    readme += "src/main.cpp\n";
    readme += "src/" + projectName + "/app/\n";
    readme += "src/" + projectName + "/application/\n";
    readme += "src/" + projectName + "/domain/\n";
    readme += "src/" + projectName + "/infrastructure/\n";
    readme += "src/" + projectName + "/presentation/\n";
    readme += "src/" + projectName + "/support/\n";
    readme += "public/\n";
    readme += "storage/\n";
    readme += "migrations/\n";
    readme += "tests/\n";
    readme += "config/\n";
    readme += "```\n\n";

    readme += "## Entry point\n\n";
    readme += "`main.cpp` stays minimal. Application bootstrapping lives in `AppBootstrap`.\n\n";

    readme += "```cpp\n";
    readme += "#include <" + projectName + "/app/AppBootstrap.hpp>\n\n";
    readme += "int main()\n";
    readme += "{\n";
    readme += "  " + projectName + "::app::AppBootstrap bootstrap;\n";
    readme += "  return bootstrap.run();\n";
    readme += "}\n";
    readme += "```\n\n";

    readme += "## Routes\n\n";
    readme += "Routes are registered from:\n\n";
    readme += "```txt\n";
    readme += "src/" + projectName + "/presentation/routes/RouteRegistry.cpp\n";
    readme += "```\n\n";

    readme += "The default generated route is:\n\n";
    readme += "```txt\n";
    readme += "GET /health\n";
    readme += "```\n\n";

    readme += "## Middleware\n\n";
    readme += "Middleware is registered from:\n\n";
    readme += "```txt\n";
    readme += "src/" + projectName + "/presentation/middleware/MiddlewareRegistry.cpp\n";
    readme += "```\n\n";

    readme += "## Production config\n\n";
    readme += "Production config starts in:\n\n";
    readme += "```txt\n";
    readme += "config/production.json\n";
    readme += "```\n\n";

    readme += "Environment variables start from:\n\n";
    readme += "```bash\n";
    readme += "cp .env.example .env\n";
    readme += "```\n\n";

    readme += "## Build manifest\n\n";
    readme += "This project uses `vix.app` as its application manifest.\n";
    readme += "Vix generates the internal CMake project automatically under `.vix/generated/app/`.\n";

    return readme;
  }

  std::string make_project_manifest_backend(
      const std::string &projectName,
      const FeaturesSelection &features)
  {
    std::string s;
    s.reserve(3000);

    s += "# Vix backend application manifest\n";
    s += "# This file is read by Vix and converted to an internal CMake project.\n";
    s += "# The generated CMakeLists.txt lives under .vix/generated/app/.\n\n";

    s += "name = \"" + projectName + "\"\n";
    s += "type = \"executable\"\n";
    s += "standard = \"c++20\"\n\n";

    s += "sources = [\n";
    s += "  \"src/main.cpp\",\n";
    s += "  \"src/" + projectName + "/app/AppBootstrap.cpp\",\n";
    s += "  \"src/" + projectName + "/presentation/routes/RouteRegistry.cpp\",\n";
    s += "  \"src/" + projectName + "/presentation/middleware/MiddlewareRegistry.cpp\",\n";
    s += "  \"src/" + projectName + "/presentation/controllers/HealthController.cpp\",\n";
    s += "]\n\n";

    s += "include_dirs = [\n";
    s += "  \"src\",\n";
    s += "]\n\n";

    s += "defines = [\n";
    s += "  \"VIX_BACKEND_APP=1\",\n";

    if (features.orm)
      s += "  \"VIX_USE_ORM=1\",\n";
    if (features.sanitizers)
      s += "  \"VIX_SANITIZERS=1\",\n";
    if (features.static_rt)
      s += "  \"VIX_LINK_STATIC=1\",\n";
    if (features.full_static)
      s += "  \"VIX_LINK_FULL_STATIC=1\",\n";

    s += "]\n\n";

    s += "compile_options = [\n";
    s += "  \"$<$<CXX_COMPILER_ID:MSVC>:/W4>\",\n";
    s += "  \"$<$<CXX_COMPILER_ID:MSVC>:/permissive->\",\n";
    s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>\",\n";
    s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>\",\n";
    s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wpedantic>\",\n";

    if (features.sanitizers)
    {
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-g3>\",\n";
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-omit-frame-pointer>\",\n";
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O1>\",\n";
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=address,undefined>\",\n";
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-sanitize-recover=undefined>\",\n";
    }

    s += "]\n\n";

    s += "link_options = [\n";

    if (features.sanitizers)
    {
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-g>\",\n";
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=address,undefined>\",\n";
    }

    if (features.full_static)
      s += "  \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-static>\",\n";
    else if (features.static_rt)
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

    if (features.orm)
      s += "  \"vix::orm\",\n";

    s += "]\n\n";

    s += "resources = [\n";
    s += "  \".env=.env\",\n";
    s += "  \"config=config\",\n";
    s += "  \"public=public\",\n";
    s += "  \"storage=storage\",\n";
    s += "]\n\n";

    s += "output_dir = \"bin\"\n";

    return s;
  }

  std::string make_vix_json_backend(const std::string &projectName)
  {
    std::string s;
    s.reserve(2200);

    s += "{\n";
    s += "  \"name\": \"" + projectName + "\",\n";
    s += "  \"version\": \"0.1.0\",\n";
    s += "  \"type\": \"application\",\n";
    s += "  \"template\": \"backend\",\n";
    s += "  \"description\": \"Production backend application generated by Vix.\",\n";
    s += "  \"license\": \"MIT\",\n";
    s += "  \"deps\": [],\n";
    s += "  \"tasks\": {\n";
    s += "    \"dev\": \"vix run\",\n";
    s += "    \"build\": \"vix build\",\n";
    s += "    \"test\": \"vix tests\",\n";
    s += "    \"health\": \"curl http://localhost:8080/health\"\n";
    s += "  },\n";
    s += "  \"production\": {\n";
    s += "    \"service\": \"" + projectName + "\",\n";
    s += "    \"health_local\": \"http://127.0.0.1:8080/health\",\n";
    s += "    \"logs_on_failure\": true\n";
    s += "  }\n";
    s += "}\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
