/**
 * @file WebMiddlewareTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/web/WebMiddlewareTemplates.hpp>
#include <vix/cli/commands/new/templates/web/WebTemplateUtils.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_web_middleware_registry_hpp(const std::string &projectName)
  {
    const std::string guard = make_web_header_guard(
        projectName,
        "MIDDLEWARE_REGISTRY");

    std::string s;
    s.reserve(1900);

    s += "/**\n";
    s += " * @file MiddlewareRegistry.hpp\n";
    s += " * @brief Middleware registry for the " + projectName + " web application.\n";
    s += " */\n\n";

    s += "#ifndef " + guard + "\n";
    s += "#define " + guard + "\n\n";

    s += "namespace vix\n";
    s += "{\n";
    s += "  class App;\n";
    s += "}\n\n";

    s += "namespace " + projectName + "::presentation::middleware\n";
    s += "{\n";
    s += "  /**\n";
    s += "   * @brief Central registry for web middleware.\n";
    s += "   *\n";
    s += "   * MiddlewareRegistry keeps the middleware stack in one place so the\n";
    s += "   * application bootstrap can stay focused on configuration and startup.\n";
    s += "   */\n";
    s += "  class MiddlewareRegistry\n";
    s += "  {\n";
    s += "  public:\n";
    s += "    /**\n";
    s += "     * @brief Register all middleware on the given Vix application.\n";
    s += "     *\n";
    s += "     * @param app Target application receiving the middleware stack.\n";
    s += "     */\n";
    s += "    static void register_all(vix::App &app);\n";
    s += "  };\n";
    s += "} // namespace " + projectName + "::presentation::middleware\n\n";

    s += "#endif // " + guard + "\n";

    return s;
  }

  std::string make_web_middleware_registry_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(3600);

    s += "/**\n";
    s += " * @file MiddlewareRegistry.cpp\n";
    s += " * @brief Middleware registration for the " + projectName + " web application.\n";
    s += " */\n\n";

    s += "#include <" + projectName + "/presentation/middleware/MiddlewareRegistry.hpp>\n\n";

    s += "#include <vix.hpp>\n";
    s += "#include <vix/log.hpp>\n";
    s += "#include <vix/middleware/app/presets.hpp>\n\n";

    s += "namespace " + projectName + "::presentation::middleware\n";
    s += "{\n";
    s += "  void MiddlewareRegistry::register_all(vix::App &app)\n";
    s += "  {\n";
    s += "    // Recommended web middleware order:\n";
    s += "    // CORS -> rate limit -> request logging -> security headers -> body limits -> auth -> routes.\n\n";

    s += "    // Security headers for HTML responses.\n";
    s += "    app.use(vix::middleware::app::security_headers_dev(false));\n\n";

    s += "    // Request logging.\n";
    s += "    app.use([](vix::Request &req, vix::Response &res, vix::App::Next next)\n";
    s += "    {\n";
    s += "      (void)res;\n\n";

    s += "      vix::log::info(\"{} {}\", req.method(), req.path());\n";
    s += "      next();\n";
    s += "    });\n\n";

    s += "    // Web marker header.\n";
    s += "    app.use([](vix::Request &req, vix::Response &res, vix::App::Next next)\n";
    s += "    {\n";
    s += "      (void)req;\n\n";

    s += "      res.header(\"X-Web\", \"true\");\n";
    s += "      next();\n";
    s += "    });\n\n";

    s += "    // Optional examples for real applications:\n";
    s += "    //\n";
    s += "    // app.use(vix::middleware::app::rate_limit({\n";
    s += "    //   .max_requests = 60,\n";
    s += "    //   .window_seconds = 60\n";
    s += "    // }));\n";
    s += "    //\n";
    s += "    // app.use(vix::middleware::app::body_limit_write_dev(1024 * 1024));\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::presentation::middleware\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
