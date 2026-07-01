/**
 * @file ModulesCommands.hpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 *
 * Business-logic handlers for the `vix modules` subcommands.
 * Each function is self-contained: it prints its own output
 * and returns true on success.
 */

#ifndef VIX_CLI_MODULES_COMMANDS_HPP
#define VIX_CLI_MODULES_COMMANDS_HPP

#include <filesystem>
#include <string>

namespace vix::commands::modules_cmd::commands
{

  /// `vix modules init` — creates modules/ + cmake/vix_modules.cmake,
  /// optionally patches root CMakeLists.txt.
  bool cmd_init(const std::filesystem::path &root, bool patchRoot);

  /// `vix modules add <module>` — scaffolds a new module skeleton under
  /// modules/<module>/, optionally auto-links it in the root CMakeLists.txt.
  bool cmd_add(
      const std::filesystem::path &root,
      const std::string &project,
      const std::string &module,
      bool patchRootLink);

  /// `vix modules check` — validates module safety rules:
  ///   - no public header includes a private src/ path
  ///   - every cross-module include has a declared CMake dependency
  bool cmd_check(const std::filesystem::path &root, const std::string &project);

  /// `vix modules list` — lists modules declared in vix.app.
  /// Shows enabled/disabled state, kind, path, and dependencies.
  bool cmd_list(const std::filesystem::path &root);

  /// `vix modules enable <module>` — enables an existing module section
  /// in vix.app by setting `enabled = true`.
  bool cmd_enable(
      const std::filesystem::path &root,
      const std::string &module);

  /// `vix modules disable <module>` — disables an existing module section
  /// in vix.app by setting `enabled = false`.
  bool cmd_disable(
      const std::filesystem::path &root,
      const std::string &module);

} // namespace vix::commands::modules_cmd::commands

#endif
