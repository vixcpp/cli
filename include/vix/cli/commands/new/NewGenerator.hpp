#pragma once

/**
 * @file NewGenerator.hpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 *
 * File-system helpers and project scaffolding routines for `vix new`.
 */

#include <filesystem>
#include <string>

#include <vix/cli/commands/new/NewTypes.hpp>

namespace vix::commands::new_cmd::generator
{

  namespace fs = std::filesystem;

  // ------------------------------------------------------------------
  // File-system helpers
  // ------------------------------------------------------------------

  bool dir_exists(const fs::path &p);
  bool dir_is_empty(const fs::path &p);

  /// Creates all directories in p (like mkdir -p). Returns false and sets err on failure.
  bool ensure_dir(const fs::path &p, std::string &err);

  /// Writes content to p, creating parent directories as needed.
  bool write_text_file(const fs::path &p, const std::string &content, std::string &err);

  // ------------------------------------------------------------------
  // Path helpers
  // ------------------------------------------------------------------

  bool is_dot_path(const std::string &s);
  std::string current_dir_name();

  // ------------------------------------------------------------------
  // Project scaffolding
  // ------------------------------------------------------------------

  /// Generates all files for an Application project under projectDir.
  bool generate_app_project(
      const fs::path &projectDir,
      const std::string &projName,
      const FeaturesSelection &features,
      std::string &err);

  /// Generates all files for a Vue + Vix Application project under projectDir.
  bool generate_vue_project(
      const fs::path &projectDir,
      const std::string &projName,
      const FeaturesSelection &features,
      std::string &err);

  /// Generates all files for a header-only Library project under projectDir.
  bool generate_lib_project(
      const fs::path &projectDir,
      const std::string &projName,
      std::string &err);

  /// Generates all files for a Vix Game project under projectDir.
  bool generate_game_project(
      const fs::path &projectDir,
      const std::string &projName,
      std::string &err);

  /// Generates all files for a production backend project under projectDir.
  bool generate_backend_project(
      const fs::path &projectDir,
      const std::string &projName,
      const FeaturesSelection &features,
      std::string &err);

  bool generate_web_project(
      const std::filesystem::path &projectDir,
      const std::string &projName,
      const FeaturesSelection &features,
      std::string &err);

  // ------------------------------------------------------------------
  // Post-generation output
  // ------------------------------------------------------------------
  void print_next_steps_app(const fs::path &projectDir, const std::string &projName);
  void print_next_steps_vue(const fs::path &projectDir, const std::string &projName);
  void print_next_steps_lib(const fs::path &projectDir, const std::string &projName);
  void print_next_steps_game(const fs::path &projectDir, const std::string &projName);
  void print_next_steps_backend(const fs::path &projectDir, const std::string &projName);
  void print_next_steps_web(
      const std::filesystem::path &projectDir,
      const std::string &projName);
} // namespace vix::commands::new_cmd::generator
