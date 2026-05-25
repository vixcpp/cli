/**
 * @file NewOutput.hpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 *
 * Minimal post-creation output for `vix new`:
 *   banner line  +  thin rule  +  inline next steps.
 * Nothing else.
 */

#ifndef VIX_CLI_NEW_OUTPUT_HPP
#define VIX_CLI_NEW_OUTPUT_HPP

#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/commands/new/NewTypes.hpp>

namespace vix::commands::new_cmd::output
{

  /// "✔  <name>  — <kind>"
  void print_banner(const std::string &projName, const std::string &kind);

  /// Full creation summary for an Application project.
  void print_creation_app(
      const std::filesystem::path &projectDir,
      const std::string &projName,
      const FeaturesSelection &features);

  /// Full creation summary for a Vue + Vix Application project.
  void print_creation_vue(
      const std::filesystem::path &projectDir,
      const std::string &projName);

  /// Full creation summary for a Library project.
  void print_creation_lib(
      const std::filesystem::path &projectDir,
      const std::string &projName);

  /// Full creation summary for a Game project.
  void print_creation_game(
      const std::filesystem::path &projectDir,
      const std::string &projName);

  /// Full creation summary for a Backend project.
  void print_creation_backend(
      const std::filesystem::path &projectDir,
      const std::string &projName,
      const FeaturesSelection &features);

  void print_creation_web(
      const std::filesystem::path &projectDir,
      const std::string &projName,
      const FeaturesSelection &features);

} // namespace vix::commands::new_cmd::output

#endif
