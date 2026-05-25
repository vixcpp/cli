/**
 * @file WebBootstrapTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/web/WebBootstrapTemplates.hpp>
#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_web_main_cpp(const std::string &projectName)
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

  std::string make_web_app_bootstrap_hpp(const std::string &projectName)
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

  std::string make_web_app_bootstrap_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(2600);

    s += "#include <" + projectName + "/app/AppBootstrap.hpp>\n";
    s += "#include <" + projectName + "/presentation/middleware/MiddlewareRegistry.hpp>\n";
    s += "#include <" + projectName + "/presentation/routes/RouteRegistry.hpp>\n\n";
    s += "#include <vix.hpp>\n";
    s += "#include <vix/log.hpp>\n\n";
    s += "namespace " + projectName + "::app\n";
    s += "{\n";
    s += "  int AppBootstrap::run()\n";
    s += "  {\n";
    s += "    vix::config::Config cfg{\".env\"};\n";
    s += "    vix::App app;\n\n";
    s += "    app.templates(\"views\");\n";
    s += "    app.static_dir(\"public\", \"/\");\n\n";
    s += "    presentation::middleware::MiddlewareRegistry::register_all(app);\n";
    s += "    presentation::routes::RouteRegistry::register_all(app);\n\n";
    s += "    vix::log::info(\"Starting " + projectName + " web app on port {}\", cfg.getServerPort());\n\n";
    s += "    app.run(cfg);\n";
    s += "    return 0;\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::app\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
