#pragma once

/**
 * @file BackendConfigTemplates.hpp
 * @author Gaspard Kirira
 *
 * Configuration file-content templates for the production backend `vix new`
 * template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_backend_production_config_json(const std::string &projectName);
  std::string make_backend_env_example(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
