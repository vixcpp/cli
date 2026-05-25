/**
 * @file BackendReadmeTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira. All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/backend/BackendReadmeTemplates.hpp>

namespace vix::commands::new_cmd::templates
{

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

} // namespace vix::commands::new_cmd::templates
