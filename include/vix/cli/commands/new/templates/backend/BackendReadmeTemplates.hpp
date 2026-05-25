#pragma once

/**
 * @file BackendReadmeTemplates.hpp
 * @author Gaspard Kirira
 *
 * README file-content templates for the production backend `vix new` template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_readme_backend(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
