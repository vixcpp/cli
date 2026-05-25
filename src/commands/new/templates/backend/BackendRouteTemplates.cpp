/**
 * @file BackendRouteTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/backend/BackendRouteTemplates.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{

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
    s.reserve(1400);

    s += "#include <" + projectName + "/presentation/routes/RouteRegistry.hpp>\n";
    s += "#include <" + projectName + "/presentation/controllers/HomeController.hpp>\n";
    s += "#include <" + projectName + "/presentation/controllers/HealthController.hpp>\n\n";
    s += "#include <vix.hpp>\n\n";
    s += "namespace " + projectName + "::presentation::routes\n";
    s += "{\n";
    s += "  void RouteRegistry::register_all(vix::App &app)\n";
    s += "  {\n";
    s += "    controllers::HomeController::register_routes(app);\n";
    s += "    controllers::HealthController::register_routes(app);\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::presentation::routes\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
