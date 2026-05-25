/**
 * @file ModulesUtils.hpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 *
 * Generic string and filesystem helpers for `vix modules`.
 * No dependency on CMake content or module business logic.
 */

#ifndef VIX_CLI_MODULES_UTILS_HPP
#define VIX_CLI_MODULES_UTILS_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vix::commands::modules_cmd::utils
{

  // String helpers
  bool starts_with(std::string_view s, std::string_view pfx);
  bool ends_with(std::string_view s, std::string_view suf);
  std::string trim(std::string s);
  std::string to_lower(std::string s);

  // Filesystem helpers
  bool exists_dir(const std::filesystem::path &p);
  bool exists_file(const std::filesystem::path &p);

  /// Creates all directories (like mkdir -p). Returns true if the path
  /// already exists or was created successfully.
  bool ensure_dir(const std::filesystem::path &p);

  /// Reads an entire file into a string. Returns nullopt on failure.
  std::optional<std::string> read_file(const std::filesystem::path &p);

  /// Writes content to p only if p does not already exist.
  bool write_file_if_missing(const std::filesystem::path &p, const std::string &content);

  /// Writes content to p, truncating any existing content.
  bool write_file_overwrite(const std::filesystem::path &p, const std::string &content);

  /// Returns all regular files under dir, recursively.
  std::vector<std::filesystem::path> list_files_recursive(const std::filesystem::path &dir);

  /// Resolves a raw --dir option to an absolute path (defaults to cwd).
  std::filesystem::path resolve_root(const std::string &dirOpt);

  /// Parses the project name from the first project() call in CMakeLists.txt.
  /// Falls back to "myproj" if not found.
  std::string detect_project_name_from_cmake(const std::filesystem::path &root);

  /// Parses the project name from the `name = ...` field in vix.app.
  /// Falls back to "myproj" if not found.
  std::string detect_project_name_from_vix_app(const std::filesystem::path &root);

  /// Detects the project name from CMakeLists.txt first, then vix.app.
  /// Falls back to "myproj" if no project name can be detected.
  std::string detect_project_name(const std::filesystem::path &root);

} // namespace vix::commands::modules_cmd::utils

#endif
