#pragma once

/**
 * @file WebConfigTemplates.hpp
 * @author Gaspard Kirira
 *
 * Configuration file-content templates for the server-rendered web
 * `vix new` template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_web_production_config_json(const std::string &projectName);
  std::string make_web_env_example(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
