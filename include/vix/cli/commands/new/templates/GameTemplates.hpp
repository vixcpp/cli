#pragma once

/**
 * @file GameTemplates.hpp
 * @author Gaspard Kirira
 *
 * File-content templates for the Vix game `vix new` template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_game_main_cpp(const std::string &projectName);

  std::string make_game_package_json(const std::string &projectName);

  std::string make_readme_game(const std::string &projectName);

  std::string make_project_manifest_game(const std::string &projectName);

  std::string make_vix_json_game(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
