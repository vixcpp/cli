/**
 * @file WebControllerTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/web/WebControllerTemplates.hpp>
#include <vix/cli/commands/new/templates/web/WebTemplateUtils.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_web_page_controller_hpp(const std::string &projectName)
  {
    const std::string guard = make_web_header_guard(
        projectName,
        "PAGE_CONTROLLER");

    std::string s;
    s.reserve(1900);

    s += "/**\n";
    s += " * @file PageController.hpp\n";
    s += " * @brief Page routes for the " + projectName + " web application.\n";
    s += " */\n\n";

    s += "#ifndef " + guard + "\n";
    s += "#define " + guard + "\n\n";

    s += "namespace vix\n";
    s += "{\n";
    s += "  class App;\n";
    s += "}\n\n";

    s += "namespace " + projectName + "::presentation::controllers\n";
    s += "{\n";
    s += "  /**\n";
    s += "   * @brief Registers server-rendered page routes.\n";
    s += "   *\n";
    s += "   * PageController owns browser-facing routes that render HTML templates\n";
    s += "   * through the Vix template engine.\n";
    s += "   */\n";
    s += "  class PageController\n";
    s += "  {\n";
    s += "  public:\n";
    s += "    /**\n";
    s += "     * @brief Register page routes on the given Vix application.\n";
    s += "     *\n";
    s += "     * @param app Target application receiving the route declarations.\n";
    s += "     */\n";
    s += "    static void register_routes(vix::App &app);\n";
    s += "  };\n";
    s += "} // namespace " + projectName + "::presentation::controllers\n\n";

    s += "#endif // " + guard + "\n";

    return s;
  }

  std::string make_web_page_controller_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(4300);

    s += "/**\n";
    s += " * @file PageController.cpp\n";
    s += " * @brief Server-rendered page route implementation for the " + projectName + " web application.\n";
    s += " */\n\n";

    s += "#include <" + projectName + "/presentation/controllers/PageController.hpp>\n\n";

    s += "#include <vix.hpp>\n";
    s += "#include <vix/template/Context.hpp>\n\n";

    s += "namespace " + projectName + "::presentation::controllers\n";
    s += "{\n";
    s += "  void PageController::register_routes(vix::App &app)\n";
    s += "  {\n";
    s += "    app.get(\"/\", [](vix::Request &req, vix::Response &res)\n";
    s += "    {\n";
    s += "      (void)req;\n\n";

    s += "      vix::template_::Context ctx;\n";
    s += "      ctx.set(\"title\", \"Home\");\n";
    s += "      ctx.set(\"app_name\", \"" + projectName + "\");\n";
    s += "      ctx.set(\"user\", \"Guest\");\n\n";

    s += "      res.render(\"index.html\", ctx);\n";
    s += "    });\n\n";

    s += "    app.get(\"/dashboard\", [](vix::Request &req, vix::Response &res)\n";
    s += "    {\n";
    s += "      (void)req;\n\n";

    s += "      vix::template_::Context ctx;\n";
    s += "      ctx.set(\"title\", \"Dashboard\");\n";
    s += "      ctx.set(\"app_name\", \"" + projectName + "\");\n";
    s += "      ctx.set(\"user\", \"Guest\");\n";
    s += "      ctx.set(\"total_orders\", 42);\n\n";

    s += "      vix::template_::Array features;\n";
    s += "      features.emplace_back(\"Server-rendered HTML\");\n";
    s += "      features.emplace_back(\"Layouts with extends\");\n";
    s += "      features.emplace_back(\"Partials with include\");\n";
    s += "      features.emplace_back(\"Static assets\");\n\n";

    s += "      ctx.set(\"features\", features);\n\n";

    s += "      res.render(\"dashboard.html\", ctx);\n";
    s += "    });\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::presentation::controllers\n";

    return s;
  }

  std::string make_web_health_controller_hpp(const std::string &projectName)
  {
    const std::string guard = make_web_header_guard(
        projectName,
        "HEALTH_CONTROLLER");

    std::string s;
    s.reserve(1800);

    s += "/**\n";
    s += " * @file HealthController.hpp\n";
    s += " * @brief Health check routes for the " + projectName + " web application.\n";
    s += " */\n\n";

    s += "#ifndef " + guard + "\n";
    s += "#define " + guard + "\n\n";

    s += "namespace vix\n";
    s += "{\n";
    s += "  class App;\n";
    s += "}\n\n";

    s += "namespace " + projectName + "::presentation::controllers\n";
    s += "{\n";
    s += "  /**\n";
    s += "   * @brief Registers health check routes for the web application.\n";
    s += "   *\n";
    s += "   * HealthController exposes a lightweight endpoint for local checks,\n";
    s += "   * deployments, reverse proxies, and monitoring tools.\n";
    s += "   */\n";
    s += "  class HealthController\n";
    s += "  {\n";
    s += "  public:\n";
    s += "    /**\n";
    s += "     * @brief Register health check routes on the given Vix application.\n";
    s += "     *\n";
    s += "     * @param app Target application receiving the route declarations.\n";
    s += "     */\n";
    s += "    static void register_routes(vix::App &app);\n";
    s += "  };\n";
    s += "} // namespace " + projectName + "::presentation::controllers\n\n";

    s += "#endif // " + guard + "\n";

    return s;
  }

  std::string make_web_health_controller_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(2400);

    s += "/**\n";
    s += " * @file HealthController.cpp\n";
    s += " * @brief Health route implementation for the " + projectName + " web application.\n";
    s += " */\n\n";

    s += "#include <" + projectName + "/presentation/controllers/HealthController.hpp>\n\n";

    s += "#include <vix.hpp>\n\n";

    s += "namespace " + projectName + "::presentation::controllers\n";
    s += "{\n";
    s += "  void HealthController::register_routes(vix::App &app)\n";
    s += "  {\n";
    s += "    app.get(\"/health\", [](vix::Request &req, vix::Response &res)\n";
    s += "    {\n";
    s += "      (void)req;\n\n";

    s += "      res.json({\n";
    s += "        \"ok\", true,\n";
    s += "        \"status\", \"ok\",\n";
    s += "        \"service\", \"" + projectName + "\",\n";
    s += "        \"template\", \"web\"\n";
    s += "      });\n";
    s += "    });\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::presentation::controllers\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
