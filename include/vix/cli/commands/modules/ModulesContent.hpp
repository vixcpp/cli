/**
 * @file ModulesContent.hpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 *
 * Module naming / validation, CMake and C++ content generators,
 * and static analysis helpers for `vix modules`.
 */
#ifndef VIX_CLI_MODULES_CONTENT_HPP
#define VIX_CLI_MODULES_CONTENT_HPP

#include <filesystem>
#include <set>
#include <string>
#include <unordered_set>

namespace vix::commands::modules_cmd::content
{
  // ------------------------------------------------------------------
  // Naming helpers
  // ------------------------------------------------------------------

  /// Replaces '-' with '_' and trims the name.
  std::string normalize_module_id(std::string name);

  /// Returns "<project>_<normalized_module>"  (CMake target name).
  std::string module_target_name(
      const std::string &project,
      const std::string &module);

  /// Returns "<project>::<normalized_module>"  (CMake alias / link name).
  std::string module_alias_name(
      const std::string &project,
      const std::string &module);

  /// Returns "Product" for "product",
  /// "UserProfile" for "user_profile".
  std::string module_class_name(
      const std::string &module);

  // ------------------------------------------------------------------
  // Validation
  // ------------------------------------------------------------------

  /// Accepts [A-Za-z0-9_-], non-empty.
  bool is_valid_module_name(
      const std::string &name);

  /// Returns true if name conflicts with a well-known tool/library name.
  bool is_reserved_module_name(
      std::string name);

  // ------------------------------------------------------------------
  // CMake content generators
  // ------------------------------------------------------------------

  /// cmake/vix_modules.cmake — the central include file added by `init`.
  std::string cmake_vix_modules_cmake_app_first();

  /// modules/<m>/CMakeLists.txt for a simple module.
  std::string module_cmakelists_txt_app_first(
      const std::string &project,
      const std::string &module);

  /// modules/<m>/CMakeLists.txt for a backend module.
  std::string module_backend_cmakelists_txt_app_first(
      const std::string &project,
      const std::string &module);

  // ------------------------------------------------------------------
  // Module manifest generators
  // ------------------------------------------------------------------

  /// modules/<m>/vix.module for a simple module.
  std::string module_manifest_app_first(
      const std::string &module);

  /// modules/<m>/vix.module for a backend module.
  std::string module_backend_manifest_app_first(
      const std::string &module);

  // ------------------------------------------------------------------
  // Simple module C++ content generators
  // ------------------------------------------------------------------

  /// modules/<m>/include/<m>/api.hpp stub.
  std::string module_public_header_app_first(
      const std::string &project,
      const std::string &module);

  /// modules/<m>/src/<m>.cpp stub.
  std::string module_impl_cpp_app_first(
      const std::string &project,
      const std::string &module);

  // ------------------------------------------------------------------
  // Backend module C++ content generators
  // ------------------------------------------------------------------

  /// modules/<m>/include/<m>/<Class>Module.hpp
  std::string module_backend_header_app_first(
      const std::string &project,
      const std::string &module);

  /// modules/<m>/src/<Class>Module.cpp
  std::string module_backend_impl_app_first(
      const std::string &project,
      const std::string &module);

  /// modules/<m>/include/<m>/controllers/<Class>Controller.hpp
  std::string module_backend_controller_header_app_first(
      const std::string &project,
      const std::string &module);

  /// modules/<m>/src/controllers/<Class>Controller.cpp
  std::string module_backend_controller_impl_app_first(
      const std::string &project,
      const std::string &module);

  // ------------------------------------------------------------------
  // CMakeLists.txt patching
  // ------------------------------------------------------------------

  /// Inserts the vix_modules.cmake include block after the project() call
  /// (idempotent via begin/end markers).
  bool patch_root_cmakelists_include(
      const std::filesystem::path &root);

  /// Inserts a conditional target_link_libraries block for the given module
  /// inside the VIX_MODULE_LINKS section (idempotent per-module).
  bool patch_root_cmakelists_link_module(
      const std::filesystem::path &root,
      const std::string &project,
      const std::string &module);

  // ------------------------------------------------------------------
  // Static analysis helpers (used by cmd_check)
  // ------------------------------------------------------------------

  /// Extracts all "<project>::<mod>" aliases declared in a module's
  /// CMakeLists.txt as explicit dependencies.
  std::unordered_set<std::string> parse_declared_deps_from_module_cmake(
      const std::filesystem::path &moduleCmake,
      const std::string &project);

  /// Returns module names referenced via "#include <other/...>" in a
  /// public header, filtering to modules that actually exist on disk.
  std::set<std::string> parse_public_includes_for_cross_module(
      const std::filesystem::path &publicHeader,
      const std::filesystem::path &modulesDir);

  /// Returns true if a public header includes a private src/ path.
  bool header_includes_private_impl(
      const std::filesystem::path &publicHeader,
      const std::filesystem::path &moduleDir);

  /// modules/<m>/tests/test_<m>.cpp for a backend module.
  std::string module_backend_test_cpp_app_first(
      const std::string &project,
      const std::string &module);

  /// modules/<m>/tests/test_<m>.cpp for a simple module.
  std::string module_test_cpp_app_first(
      const std::string &project,
      const std::string &module);

} // namespace vix::commands::modules_cmd::content

#endif // VIX_CLI_MODULES_CONTENT_HPP
