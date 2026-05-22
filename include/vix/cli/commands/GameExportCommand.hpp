/**
 *
 *  @file GameExportCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  vix game export command.
 *
 */
#ifndef VIX_CLI_COMMANDS_GAME_EXPORT_COMMAND_HPP
#define VIX_CLI_COMMANDS_GAME_EXPORT_COMMAND_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace vix::commands::GameCommand
{
  /**
   * @brief Options accepted by `vix game export`.
   */
  struct ExportOptions
  {
    /**
     * @brief Project root to export.
     */
    std::filesystem::path project_root{"."};

    /**
     * @brief Optional output directory override.
     */
    std::filesystem::path output_directory{};

    /**
     * @brief Optional export name override.
     */
    std::string name{};

    /**
     * @brief Whether an existing export directory can be overwritten.
     */
    bool overwrite{true};

    /**
     * @brief Whether assets should be copied.
     */
    bool copy_assets{true};
  };

  /**
   * @brief Parse options for `vix game export`.
   */
  [[nodiscard]] ExportOptions parse_export_options(
      const std::vector<std::string> &args);

  /**
   * @brief Run `vix game export`.
   */
  [[nodiscard]] int export_game(
      const std::vector<std::string> &args);

  /**
   * @brief Run the `vix game` command group.
   */
  [[nodiscard]] int run(
      const std::vector<std::string> &args);

  /**
   * @brief Print help for `vix game`.
   */
  [[nodiscard]] int help();

} // namespace vix::commands::GameCommand

#endif // VIX_CLI_COMMANDS_GAME_EXPORT_COMMAND_HPP
