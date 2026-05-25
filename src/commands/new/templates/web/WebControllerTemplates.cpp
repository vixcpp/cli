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

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_web_page_controller_hpp(const std::string &projectName)
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
    s += "  class PageController\n";
    s += "  {\n";
    s += "  public:\n";
    s += "    static void register_routes(vix::App &app);\n";
    s += "  };\n";
    s += "} // namespace " + projectName + "::presentation::controllers\n";

    return s;
  }

  std::string make_web_page_controller_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(3200);

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
    s += "      ctx.set(\"user\", \"Gaspard\");\n\n";
    s += "      res.render(\"index.html\", ctx);\n";
    s += "    });\n\n";
    s += "    app.get(\"/dashboard\", [](vix::Request &req, vix::Response &res)\n";
    s += "    {\n";
    s += "      (void)req;\n\n";
    s += "      vix::template_::Context ctx;\n";
    s += "      ctx.set(\"title\", \"Dashboard\");\n";
    s += "      ctx.set(\"app_name\", \"" + projectName + "\");\n";
    s += "      ctx.set(\"user\", \"Gaspard\");\n";
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

  std::string make_web_health_controller_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(1600);

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
