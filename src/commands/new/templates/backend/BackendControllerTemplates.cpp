/**
 * @file BackendControllerTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/backend/BackendControllerTemplates.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{

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

} // namespace vix::commands::new_cmd::templates
