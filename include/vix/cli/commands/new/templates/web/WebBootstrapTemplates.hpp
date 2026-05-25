#pragma once

/**
 * @file WebBootstrapTemplates.hpp
 * @author Gaspard Kirira
 *
 * Entry-point and bootstrap file-content templates for the server-rendered web
 * `vix new` template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_web_main_cpp(const std::string &projectName);
  std::string make_web_app_bootstrap_hpp(const std::string &projectName);
  std::string make_web_app_bootstrap_cpp(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
