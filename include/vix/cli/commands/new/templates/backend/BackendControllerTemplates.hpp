#pragma once

/**
 * @file BackendControllerTemplates.hpp
 * @author Gaspard Kirira
 *
 * Controller file-content templates for the production backend `vix new`
 * template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_backend_health_controller_hpp(const std::string &projectName);
  std::string make_backend_health_controller_cpp(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
