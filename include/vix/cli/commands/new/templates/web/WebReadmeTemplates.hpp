#pragma once

/**
 * @file WebReadmeTemplates.hpp
 * @author Gaspard Kirira
 *
 * README file-content templates for the server-rendered web
 * `vix new` template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_readme_web(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
