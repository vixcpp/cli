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

#include <vix/cli/commands/modules/ModulesTypes.hpp>

namespace vix::commands::modules_cmd::commands
{
  /**
   * @brief Initialize module support in a project.
   *
   * This command creates the `modules/` directory and the central
   * `cmake/vix_modules.cmake` loader used by module-enabled projects.
   * When requested, it also patches the root CMake project when a classic
   * `CMakeLists.txt` project is detected.
   *
   * For `vix.app` projects, the root CMake patch is skipped because the
   * internal generated CMake project is responsible for loading enabled
   * application modules.
   *
   * @param root Project root directory.
   * @param patchRoot Whether to patch the root CMake project when possible.
   * @return true on success, false otherwise.
   */
  bool cmd_init(
      const std::filesystem::path &root,
      bool patchRoot);

  /**
   * @brief Create a new application module.
   *
   * This command creates the module directory, writes the generated module
   * files, and optionally registers the module in the root build entry point.
   * The exact generated layout depends on the selected module options.
   *
   * Normal modules use the standard C++ module layout. Backend and service
   * applications receive routed modules. WebSocket modules receive a
   * workflow-aware layout because they may need long-lived runtime behavior
   * instead of simple request/response route registration.
   *
   * @param root Project root directory.
   * @param project Project name used for generated targets and namespaces.
   * @param module Module name requested by the user.
   * @param options Module generation options.
   * @return true on success, false otherwise.
   */
  bool cmd_add(
      const std::filesystem::path &root,
      const std::string &project,
      const std::string &module,
      const AddModuleOptions &options);

  /**
   * @brief Validate module safety and dependency rules.
   *
   * This command checks the module tree for common structural errors:
   * public headers must not include private implementation files, and
   * cross-module includes must be backed by explicit module dependencies.
   *
   * For `vix.app` projects, it also validates enabled module declarations,
   * missing module folders, missing CMakeLists files, missing `vix.module`
   * manifests, disabled dependencies, undeclared dependencies, and circular
   * dependencies.
   *
   * @param root Project root directory.
   * @param project Project name used to resolve generated CMake aliases.
   * @return true when all checks pass, false otherwise.
   */
  bool cmd_check(
      const std::filesystem::path &root,
      const std::string &project);

  /**
   * @brief List modules declared in a vix.app project.
   *
   * The output shows enabled or disabled state, module kind, module path,
   * filesystem status, and declared dependencies.
   *
   * @param root Project root directory.
   * @return true on success, false otherwise.
   */
  bool cmd_list(
      const std::filesystem::path &root);

  /**
   * @brief Enable an existing module in vix.app.
   *
   * This command updates the matching `[module.<name>]` section by setting
   * `enabled = true`. It does not create the module and does not modify
   * module source files.
   *
   * @param root Project root directory.
   * @param module Module name to enable.
   * @return true on success, false otherwise.
   */
  bool cmd_enable(
      const std::filesystem::path &root,
      const std::string &module);

  /**
   * @brief Disable an existing module in vix.app.
   *
   * This command updates the matching `[module.<name>]` section by setting
   * `enabled = false`. The module folder remains on disk, but the generated
   * application CMake project will not load it while it is disabled.
   *
   * @param root Project root directory.
   * @param module Module name to disable.
   * @return true on success, false otherwise.
   */
  bool cmd_disable(
      const std::filesystem::path &root,
      const std::string &module);

} // namespace vix::commands::modules_cmd::commands

#endif // VIX_CLI_MODULES_COMMANDS_HPP
