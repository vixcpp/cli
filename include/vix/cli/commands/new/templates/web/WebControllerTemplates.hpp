#pragma once

/**
 * @file WebControllerTemplates.hpp
 * @author Gaspard Kirira
 *
 * Controller file-content templates for the server-rendered web
 * `vix new` template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_web_page_controller_hpp(const std::string &projectName);
  std::string make_web_page_controller_cpp(const std::string &projectName);

  std::string make_web_health_controller_hpp(const std::string &projectName);
  std::string make_web_health_controller_cpp(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
