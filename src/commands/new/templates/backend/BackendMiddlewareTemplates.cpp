/**
 * @file BackendMiddlewareTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/backend/BackendMiddlewareTemplates.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{

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
    s.reserve(2600);

    s += "#include <" + projectName + "/presentation/middleware/MiddlewareRegistry.hpp>\n\n";
    s += "#include <vix.hpp>\n";
    s += "#include <vix/log.hpp>\n";
    s += "#include <vix/middleware.hpp>\n\n";
    s += "namespace " + projectName + "::presentation::middleware\n";
    s += "{\n";
    s += "  void MiddlewareRegistry::register_all(vix::App &app)\n";
    s += "  {\n";
    s += "    // Recommended production order:\n";
    s += "    // CORS -> rate limit -> request logging -> security headers -> body limits -> auth -> routes.\n\n";
    s += "    // Security headers.\n";
    s += "    app.use(vix::middleware::app::security_headers_dev(false));\n\n";
    s += "    // Request logging.\n";
    s += "    app.use([](vix::Request &req, vix::Response &res, vix::App::Next next)\n";
    s += "    {\n";
    s += "      (void)res;\n\n";
    s += "      vix::log::info(\"{} {}\", req.method(), req.path());\n";
    s += "      next();\n";
    s += "    });\n\n";
    s += "    // Basic API marker header.\n";
    s += "    app.use(\"/api\", [](vix::Request &req, vix::Response &res, vix::App::Next next)\n";
    s += "    {\n";
    s += "      (void)req;\n\n";
    s += "      res.header(\"X-API\", \"true\");\n";
    s += "      next();\n";
    s += "    });\n\n";
    s += "    // Optional examples for real applications:\n";
    s += "    //\n";
    s += "    // app.use(vix::middleware::app::cors_dev({\n";
    s += "    //   \"http://localhost:5173\",\n";
    s += "    //   \"http://127.0.0.1:5173\"\n";
    s += "    // }));\n";
    s += "    //\n";
    s += "    // app.use(vix::middleware::app::rate_limit({\n";
    s += "    //   .max_requests = 60,\n";
    s += "    //   .window_seconds = 60\n";
    s += "    // }));\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::presentation::middleware\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
