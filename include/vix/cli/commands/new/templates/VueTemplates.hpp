#pragma once

/**
 * @file VueTemplates.hpp
 * @author Gaspard Kirira
 *
 * File-content templates for the Vue + Vix `vix new` template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_readme_vue_app(const std::string &projectName);

  std::string make_vix_json_vue_app(const std::string &projectName);

  std::string make_vue_package_json(const std::string &projectName);

  std::string make_vue_index_html(const std::string &projectName);

  std::string make_vue_vite_config();

  std::string make_vue_main_js();

  std::string make_vue_app_vue();

} // namespace vix::commands::new_cmd::templates
