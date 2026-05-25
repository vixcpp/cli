#pragma once

/**
 * @file BackendTestTemplates.hpp
 * @author Gaspard Kirira
 *
 * Test file-content templates for the production backend `vix new` template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_backend_basic_test_cpp(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
