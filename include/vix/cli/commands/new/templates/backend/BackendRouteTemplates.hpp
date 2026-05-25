#pragma once

/**
 * @file BackendRouteTemplates.hpp
 * @author Gaspard Kirira
 *
 * Route registry file-content templates for the production backend `vix new`
 * template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_backend_route_registry_hpp(const std::string &projectName);
  std::string make_backend_route_registry_cpp(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
