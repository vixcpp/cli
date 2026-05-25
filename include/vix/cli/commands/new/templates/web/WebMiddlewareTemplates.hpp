#pragma once

/**
 * @file WebMiddlewareTemplates.hpp
 * @author Gaspard Kirira
 *
 * Middleware registry file-content templates for the server-rendered web
 * `vix new` template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_web_middleware_registry_hpp(const std::string &projectName);
  std::string make_web_middleware_registry_cpp(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
