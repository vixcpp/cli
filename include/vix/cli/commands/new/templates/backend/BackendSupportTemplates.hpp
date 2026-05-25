#pragma once

/**
 * @file BackendSupportTemplates.hpp
 * @author Gaspard Kirira
 *
 * Support helper file-content templates for the production backend
 * `vix new` template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_backend_http_responses_hpp(const std::string &projectName);
  std::string make_backend_http_responses_cpp(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
