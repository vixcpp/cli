#pragma once

/**
 * @file LibTemplates.hpp
 * @author Gaspard Kirira
 *
 * File-content templates for the header-only library `vix new` template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  /// include/<name>/<name>.hpp for a header-only library project.
  std::string make_lib_header(const std::string &name);

  /// tests/test_basic.cpp for a library project.
  std::string make_basic_test_cpp_lib(const std::string &name);

  /// examples/basic.cpp for a library project.
  std::string make_basic_example_cpp_lib(const std::string &name);

  /// examples/CMakeLists.txt for a library project.
  std::string make_examples_cmakelists_lib(const std::string &name);

  std::string make_readme_lib(const std::string &name);

  std::string make_cmake_presets_json_lib();

  std::string make_project_manifest_lib(const std::string &name);

  std::string make_vix_json_lib(const std::string &name);

  std::string make_cmakelists_lib(const std::string &name);

} // namespace vix::commands::new_cmd::templates
