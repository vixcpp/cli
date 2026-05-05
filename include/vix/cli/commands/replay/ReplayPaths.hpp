/**
 *
 *  @file ReplayPaths.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_CLI_COMMANDS_REPLAY_PATHS_HPP
#define VIX_CLI_COMMANDS_REPLAY_PATHS_HPP

#include <filesystem>
#include <string>

namespace vix::commands::replay
{

  namespace fs = std::filesystem;

  /**
   * @brief Group of filesystem paths used by one replay run.
   *
   * Replay files are stored under:
   *
   * .vix/runs/<id>/
   */
  struct ReplayRunPaths
  {
    /**
     * @brief Replay id.
     */
    std::string id;

    /**
     * @brief Root directory that contains all replay runs.
     *
     * Example:
     * .vix/runs
     */
    fs::path runs_root;

    /**
     * @brief Directory dedicated to this replay run.
     *
     * Example:
     * .vix/runs/2026-05-05-18-42-11-a91f
     */
    fs::path run_dir;

    /**
     * @brief Main replay metadata file.
     *
     * Example:
     * .vix/runs/<id>/run.json
     */
    fs::path record_file;

    /**
     * @brief Captured stdout log file.
     *
     * Example:
     * .vix/runs/<id>/stdout.log
     */
    fs::path stdout_file;

    /**
     * @brief Captured stderr log file.
     *
     * Example:
     * .vix/runs/<id>/stderr.log
     */
    fs::path stderr_file;

    /**
     * @brief Combined output log file.
     *
     * Example:
     * .vix/runs/<id>/combined.log
     */
    fs::path combined_file;

    /**
     * @brief Small marker file used to resolve the last replay run.
     *
     * Example:
     * .vix/runs/latest
     */
    fs::path latest_file;
  };

  /**
   * @brief Return the local Vix directory for a project.
   *
   * @param baseDir Base directory.
   * @return Path to baseDir/.vix.
   */
  fs::path replay_vix_dir(const fs::path &baseDir);

  /**
   * @brief Return the replay runs root directory for a project.
   *
   * @param baseDir Base directory.
   * @return Path to baseDir/.vix/runs.
   */
  fs::path replay_runs_root(const fs::path &baseDir);

  /**
   * @brief Return the latest marker file path.
   *
   * @param baseDir Base directory.
   * @return Path to baseDir/.vix/runs/latest.
   */
  fs::path replay_latest_file(const fs::path &baseDir);

  /**
   * @brief Return the directory for one replay id.
   *
   * @param baseDir Base directory.
   * @param id Replay id.
   * @return Path to baseDir/.vix/runs/id.
   */
  fs::path replay_run_dir(const fs::path &baseDir, const std::string &id);

  /**
   * @brief Build all paths used by one replay run.
   *
   * @param baseDir Base directory.
   * @param id Replay id.
   * @return Full ReplayRunPaths structure.
   */
  ReplayRunPaths make_replay_run_paths(const fs::path &baseDir, const std::string &id);

  /**
   * @brief Ensure that the replay runs root exists.
   *
   * @param baseDir Base directory.
   * @param err Error message written on failure.
   * @return true on success.
   */
  bool ensure_replay_root(const fs::path &baseDir, std::string &err);

  /**
   * @brief Ensure that the directory for one replay run exists.
   *
   * @param paths Replay run paths.
   * @param err Error message written on failure.
   * @return true on success.
   */
  bool ensure_replay_run_dir(const ReplayRunPaths &paths, std::string &err);

  /**
   * @brief Return true when the given replay id looks safe for filesystem usage.
   *
   * Allowed characters:
   * - letters
   * - digits
   * - dash
   * - underscore
   *
   * @param id Replay id.
   * @return true when the id is safe.
   */
  bool is_safe_replay_id(const std::string &id);

  /**
   * @brief Return true when the replay run directory exists.
   *
   * @param baseDir Base directory.
   * @param id Replay id.
   * @return true if the run directory exists.
   */
  bool replay_run_exists(const fs::path &baseDir, const std::string &id);

} // namespace vix::commands::replay

#endif // VIX_CLI_COMMANDS_REPLAY_PATHS_HPP
