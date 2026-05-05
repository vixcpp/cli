/**
 *
 *  @file ReplayRunner.hpp
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
#ifndef VIX_CLI_COMMANDS_REPLAY_RUNNER_HPP
#define VIX_CLI_COMMANDS_REPLAY_RUNNER_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/commands/replay/ReplayProcess.hpp>
#include <vix/cli/commands/replay/ReplayRecord.hpp>
#include <vix/cli/commands/replay/ReplayStore.hpp>

namespace vix::commands::replay
{

  namespace fs = std::filesystem;

  /**
   * @brief Selector used to resolve which replay record should be executed.
   */
  struct ReplaySelector
  {
    /**
     * @brief Raw selector provided by the user.
     *
     * Examples:
     * - last
     * - latest
     * - failed
     * - 2026-05-05-18-42-11-a91f
     */
    std::string value{"last"};
  };

  /**
   * @brief Options used by the high-level replay runner.
   */
  struct ReplayRunnerOptions
  {
    /**
     * @brief Base directory where .vix/runs is stored.
     */
    fs::path base_dir;

    /**
     * @brief Replay selector.
     */
    ReplaySelector selector{};

    /**
     * @brief Override the recorded working directory.
     */
    fs::path cwd_override;

    /**
     * @brief Extra arguments appended to the replay command.
     */
    std::vector<std::string> extra_args;

    /**
     * @brief Environment variables added during replay.
     */
    std::vector<ReplayEnvVar> extra_env;

    /**
     * @brief Print the selected record summary before replaying.
     */
    bool print_summary{true};

    /**
     * @brief Print the replay command before executing it.
     */
    bool print_command{true};

    /**
     * @brief Do not execute the replay command.
     */
    bool dry_run{false};
  };

  /**
   * @brief Result returned by the high-level replay runner.
   */
  struct ReplayRunnerResult
  {
    /**
     * @brief Replay record selected for execution.
     */
    ReplayRecord record{};

    /**
     * @brief Process execution result.
     */
    ReplayProcessRunResult process{};

    /**
     * @brief True when a record was resolved and execution was attempted.
     */
    bool launched{false};

    /**
     * @brief True when the replay command completed successfully.
     */
    bool success{false};
  };

  /**
   * @brief Resolve a replay selector to a replay record.
   *
   * @param baseDir Base directory where .vix/runs is stored.
   * @param selector Replay selector.
   * @param record Output replay record.
   * @param err Error message written on failure.
   * @return true on success.
   */
  bool resolve_replay_record(
      const fs::path &baseDir,
      const ReplaySelector &selector,
      ReplayRecord &record,
      std::string &err);

  /**
   * @brief Resolve the latest failed replay record.
   *
   * @param baseDir Base directory where .vix/runs is stored.
   * @param record Output replay record.
   * @param err Error message written on failure.
   * @return true on success.
   */
  bool resolve_latest_failed_replay_record(
      const fs::path &baseDir,
      ReplayRecord &record,
      std::string &err);

  /**
   * @brief Replay a selected record.
   *
   * @param options Runner options.
   * @param result Output runner result.
   * @param err Error message written on failure.
   * @return true when the replay command could be launched.
   */
  bool run_replay(
      const ReplayRunnerOptions &options,
      ReplayRunnerResult &result,
      std::string &err);

  /**
   * @brief Return the default base directory used by replay commands.
   *
   * @return Current working directory.
   */
  fs::path default_replay_base_dir();

} // namespace vix::commands::replay

#endif // VIX_CLI_COMMANDS_REPLAY_RUNNER_HPP
