/**
 *
 *  @file ReplayProcess.hpp
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
#ifndef VIX_CLI_COMMANDS_REPLAY_PROCESS_HPP
#define VIX_CLI_COMMANDS_REPLAY_PROCESS_HPP

#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/commands/replay/ReplayRecord.hpp>

namespace vix::commands::replay
{

  namespace fs = std::filesystem;

  /**
   * @brief Options used when replaying a recorded process.
   */
  struct ReplayProcessOptions
  {
    /**
     * @brief Override the recorded working directory.
     *
     * When empty, record.cwd is used.
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
     * @brief Print the command before executing it.
     */
    bool print_command{true};

    /**
     * @brief Do not execute the command, only print it.
     */
    bool dry_run{false};
  };

  /**
   * @brief Result of a replayed process.
   */
  struct ReplayProcessRunResult
  {
    /**
     * @brief Normalized exit code.
     */
    int exit_code{0};

    /**
     * @brief Raw platform-specific status.
     */
    int raw_status{0};

    /**
     * @brief Command executed by the replay process.
     */
    std::string command;

    /**
     * @brief Working directory used during replay.
     */
    fs::path cwd;
  };

  /**
   * @brief Return true when a replay record has enough data to be replayed.
   *
   * @param record Replay record.
   * @param err Error message written on failure.
   * @return true when replayable.
   */
  bool can_replay_process(const ReplayRecord &record, std::string &err);

  /**
   * @brief Build the shell command used for replay.
   *
   * @param record Replay record.
   * @param options Replay options.
   * @return Shell command.
   */
  std::string build_replay_process_command(
      const ReplayRecord &record,
      const ReplayProcessOptions &options);

  /**
   * @brief Replay a recorded process.
   *
   * This executes the recorded resolved command from the original working
   * directory unless cwd_override is provided.
   *
   * @param record Replay record.
   * @param options Replay options.
   * @param result Replay execution result.
   * @param err Error message written on failure.
   * @return true when the replay command was launched.
   */
  bool run_replay_process(
      const ReplayRecord &record,
      const ReplayProcessOptions &options,
      ReplayProcessRunResult &result,
      std::string &err);

  /**
   * @brief Quote a value for shell usage.
   *
   * @param value Raw string.
   * @return Shell-safe string.
   */
  std::string replay_shell_quote(const std::string &value);

  /**
   * @brief Normalize a platform-specific process status.
   *
   * @param status Raw status.
   * @return Normalized exit code.
   */
  int replay_normalize_exit_code(int status);

} // namespace vix::commands::replay

#endif // VIX_CLI_COMMANDS_REPLAY_PROCESS_HPP
