#pragma once

/**
 * @file WebTestTemplates.hpp
 * @author Gaspard Kirira
 *
 * Test file-content templates for the server-rendered web
 * `vix new` template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_web_basic_test_cpp(const std::string &projectName);
  std::string make_web_tests_manifest(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
