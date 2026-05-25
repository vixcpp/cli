#pragma once

/**
 * @file BackendTemplates.hpp
 * @author Gaspard Kirira
 *
 * File-content templates for the production backend `vix new` template.
 */

#include <string>

#include <vix/cli/commands/new/NewTypes.hpp>

namespace vix::commands::new_cmd::templates
{

  std::string make_backend_main_cpp(const std::string &projectName);

  std::string make_backend_app_bootstrap_hpp(const std::string &projectName);
  std::string make_backend_app_bootstrap_cpp(const std::string &projectName);

  std::string make_backend_route_registry_hpp(const std::string &projectName);
  std::string make_backend_route_registry_cpp(const std::string &projectName);

  std::string make_backend_middleware_registry_hpp(const std::string &projectName);
  std::string make_backend_middleware_registry_cpp(const std::string &projectName);

  std::string make_backend_health_controller_hpp(const std::string &projectName);
  std::string make_backend_health_controller_cpp(const std::string &projectName);

  std::string make_backend_production_config_json();
  std::string make_backend_env_example();
  std::string make_backend_basic_test_cpp(const std::string &projectName);

  std::string make_readme_backend(const std::string &projectName);

  std::string make_project_manifest_backend(
      const std::string &projectName,
      const FeaturesSelection &features);

  std::string make_vix_json_backend(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
