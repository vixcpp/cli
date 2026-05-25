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
#include <vix/cli/commands/new/templates/backend/BackendTemplateUtils.hpp>

#include <cctype>
#include <string>

namespace vix::commands::new_cmd::templates
{
  std::string make_backend_home_controller_hpp(const std::string &projectName)
  {
    const std::string guard = make_backend_header_guard(projectName, "HOME_CONTROLLER");

    std::string s;
    s.reserve(1700);

    s += "/**\n";
    s += " * @file HomeController.hpp\n";
    s += " * @brief Home API routes for the " + projectName + " backend.\n";
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
    s += "   * @brief Registers public home routes for the backend API.\n";
    s += "   *\n";
    s += "   * HomeController owns lightweight routes that are useful for verifying\n";
    s += "   * that the generated backend is reachable and correctly wired.\n";
    s += "   */\n";
    s += "  class HomeController\n";
    s += "  {\n";
    s += "  public:\n";
    s += "    /**\n";
    s += "     * @brief Register the home routes on the given Vix application.\n";
    s += "     *\n";
    s += "     * @param app Target application receiving the routes.\n";
    s += "     */\n";
    s += "    static void register_routes(vix::App &app);\n";
    s += "  };\n";
    s += "} // namespace " + projectName + "::presentation::controllers\n\n";

    s += "#endif // " + guard + "\n";

    return s;
  }

  std::string make_backend_home_controller_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(2200);

    s += "/**\n";
    s += " * @file HomeController.cpp\n";
    s += " * @brief Home route implementation for the " + projectName + " backend.\n";
    s += " */\n\n";

    s += "#include <" + projectName + "/presentation/controllers/HomeController.hpp>\n\n";
    s += "#include <vix.hpp>\n\n";

    s += "namespace " + projectName + "::presentation::controllers\n";
    s += "{\n";
    s += "  void HomeController::register_routes(vix::App &app)\n";
    s += "  {\n";
    s += "    app.get(\"/api\", [](vix::Request &req, vix::Response &res)\n";
    s += "    {\n";
    s += "      (void)req;\n\n";
    s += "      res.json({\n";
    s += "        \"ok\", true,\n";
    s += "        \"service\", \"" + projectName + "\",\n";
    s += "        \"message\", \"Vix backend is running\"\n";
    s += "      });\n";
    s += "    });\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::presentation::controllers\n";

    return s;
  }

  std::string make_backend_health_controller_hpp(const std::string &projectName)
  {
    const std::string guard = make_backend_header_guard(projectName, "HEALTH_CONTROLLER");

    std::string s;
    s.reserve(1700);

    s += "/**\n";
    s += " * @file HealthController.hpp\n";
    s += " * @brief Health check routes for the " + projectName + " backend.\n";
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
    s += "   * @brief Registers health check routes for the backend application.\n";
    s += "   *\n";
    s += "   * HealthController exposes routes intended for local checks, reverse\n";
    s += "   * proxies, deployment scripts, and production monitoring.\n";
    s += "   */\n";
    s += "  class HealthController\n";
    s += "  {\n";
    s += "  public:\n";
    s += "    /**\n";
    s += "     * @brief Register health check routes on the given Vix application.\n";
    s += "     *\n";
    s += "     * @param app Target application receiving the routes.\n";
    s += "     */\n";
    s += "    static void register_routes(vix::App &app);\n";
    s += "  };\n";
    s += "} // namespace " + projectName + "::presentation::controllers\n\n";

    s += "#endif // " + guard + "\n";

    return s;
  }

  std::string make_backend_health_controller_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(2600);

    s += "/**\n";
    s += " * @file HealthController.cpp\n";
    s += " * @brief Health route implementation for the " + projectName + " backend.\n";
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
    s += "        \"service\", \"" + projectName + "\"\n";
    s += "      });\n";
    s += "    });\n\n";

    s += "    app.get(\"/api/health\", [](vix::Request &req, vix::Response &res)\n";
    s += "    {\n";
    s += "      (void)req;\n\n";
    s += "      res.json({\n";
    s += "        \"ok\", true,\n";
    s += "        \"status\", \"ok\",\n";
    s += "        \"service\", \"" + projectName + "\",\n";
    s += "        \"api\", true\n";
    s += "      });\n";
    s += "    });\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::presentation::controllers\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
