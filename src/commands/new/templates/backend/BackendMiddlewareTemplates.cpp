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
    s.reserve(900);

    s += "#include <" + projectName + "/presentation/middleware/MiddlewareRegistry.hpp>\n\n";
    s += "#include <vix.hpp>\n\n";
    s += "namespace " + projectName + "::presentation::middleware\n";
    s += "{\n";
    s += "  void MiddlewareRegistry::register_all(vix::App & /*app*/)\n";
    s += "  {\n";
    s += "    // Register production middleware here.\n";
    s += "    // Examples: logging, request id, CORS, authentication, rate limiting.\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::presentation::middleware\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
