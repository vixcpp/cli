/**
 * @file WebRouteTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/web/WebRouteTemplates.hpp>
#include <vix/cli/commands/new/templates/web/WebTemplateUtils.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_web_route_registry_hpp(const std::string &projectName)
  {
    const std::string guard = make_web_header_guard(
        projectName,
        "ROUTE_REGISTRY");

    std::string s;
    s.reserve(1900);

    s += "/**\n";
    s += " * @file RouteRegistry.hpp\n";
    s += " * @brief Route registry for the " + projectName + " web application.\n";
    s += " */\n\n";

    s += "#ifndef " + guard + "\n";
    s += "#define " + guard + "\n\n";

    s += "namespace vix\n";
    s += "{\n";
    s += "  class App;\n";
    s += "}\n\n";

    s += "namespace " + projectName + "::presentation::routes\n";
    s += "{\n";
    s += "  /**\n";
    s += "   * @brief Central registry for web routes.\n";
    s += "   *\n";
    s += "   * RouteRegistry keeps route registration grouped in one place so the\n";
    s += "   * application bootstrap does not need to know about individual controllers.\n";
    s += "   */\n";
    s += "  class RouteRegistry\n";
    s += "  {\n";
    s += "  public:\n";
    s += "    /**\n";
    s += "     * @brief Register all routes on the given Vix application.\n";
    s += "     *\n";
    s += "     * @param app Target application receiving the route declarations.\n";
    s += "     */\n";
    s += "    static void register_all(vix::App &app);\n";
    s += "  };\n";
    s += "} // namespace " + projectName + "::presentation::routes\n\n";

    s += "#endif // " + guard + "\n";

    return s;
  }

  std::string make_web_route_registry_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(2000);

    s += "/**\n";
    s += " * @file RouteRegistry.cpp\n";
    s += " * @brief Route registration implementation for the " + projectName + " web application.\n";
    s += " */\n\n";

    s += "#include <" + projectName + "/presentation/routes/RouteRegistry.hpp>\n";
    s += "#include <" + projectName + "/presentation/controllers/PageController.hpp>\n";
    s += "#include <" + projectName + "/presentation/controllers/HealthController.hpp>\n\n";

    s += "#include <vix.hpp>\n\n";

    s += "namespace " + projectName + "::presentation::routes\n";
    s += "{\n";
    s += "  void RouteRegistry::register_all(vix::App &app)\n";
    s += "  {\n";
    s += "    controllers::PageController::register_routes(app);\n";
    s += "    controllers::HealthController::register_routes(app);\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::presentation::routes\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
