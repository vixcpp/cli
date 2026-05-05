/**
 *
 *  @file ReplayRecord.hpp
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
#ifndef VIX_CLI_COMMANDS_REPLAY_RECORD_HPP
#define VIX_CLI_COMMANDS_REPLAY_RECORD_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/commands/replay/ReplayTypes.hpp>

namespace vix::commands::replay
{

  /**
   * @brief One environment variable captured for a replayable execution.
   */
  struct ReplayEnvVar
  {
    /**
     * @brief Environment variable name.
     */
    std::string name;

    /**
     * @brief Environment variable value.
     */
    std::string value;
  };

  /**
   * @brief Basic timing information for a replayable execution.
   */
  struct ReplayTiming
  {
    /**
     * @brief ISO-like start timestamp.
     *
     * Example:
     * 2026-05-05T18:42:11Z
     */
    std::string started_at;

    /**
     * @brief ISO-like finish timestamp.
     */
    std::string finished_at;

    /**
     * @brief Total execution duration in milliseconds.
     */
    std::int64_t duration_ms{0};
  };

  /**
   * @brief Captured process result.
   */
  struct ReplayProcessResult
  {
    /**
     * @brief Normalized process exit code.
     */
    int exit_code{0};

    /**
     * @brief Raw platform-specific process status.
     */
    int raw_status{0};

    /**
     * @brief True when the process was terminated by a signal.
     */
    bool terminated_by_signal{false};

    /**
     * @brief Signal number when terminated_by_signal is true.
     */
    int signal{0};
  };

  /**
   * @brief Paths to logs captured for one replay record.
   */
  struct ReplayLogPaths
  {
    /**
     * @brief Path to the captured stdout log.
     */
    std::filesystem::path stdout_log;

    /**
     * @brief Path to the captured stderr log.
     */
    std::filesystem::path stderr_log;

    /**
     * @brief Path to the combined output log.
     */
    std::filesystem::path combined_log;
  };

  /**
   * @brief Full replayable execution record.
   *
   * A ReplayRecord is the durable description of a `vix run`, `vix dev`,
   * or future runtime execution. It stores enough information to explain
   * what happened and to replay the same command later.
   */
  struct ReplayRecord
  {
    /**
     * @brief Stable replay identifier.
     *
     * Example:
     * 2026-05-05-18-42-11-a91f
     */
    std::string id;

    /**
     * @brief Replay schema version.
     *
     * Increment this when the JSON structure changes.
     */
    std::uint32_t schema_version{1};

    /**
     * @brief Command mode that produced this record.
     */
    ReplayMode mode{ReplayMode::Unknown};

    /**
     * @brief Input target kind.
     */
    ReplayTargetKind target_kind{ReplayTargetKind::Unknown};

    /**
     * @brief Final execution status.
     */
    ReplayStatus status{ReplayStatus::Unknown};

    /**
     * @brief Failure category, when status is not success.
     */
    ReplayErrorKind error_kind{ReplayErrorKind::None};

    /**
     * @brief Original working directory.
     */
    std::filesystem::path cwd;

    /**
     * @brief Project directory when available.
     */
    std::filesystem::path project_dir;

    /**
     * @brief Main target path.
     *
     * For script mode, this is the .cpp file.
     * For manifest mode, this is the .vix file.
     * For binary mode, this is the executable path.
     */
    std::filesystem::path target_path;

    /**
     * @brief Reconstructed public command.
     *
     * Example:
     * vix dev server.cpp
     */
    std::string command;

    /**
     * @brief Executable command used internally, when different from command.
     */
    std::string resolved_command;

    /**
     * @brief Arguments passed to Vix.
     */
    std::vector<std::string> vix_args;

    /**
     * @brief Arguments forwarded to the running application.
     */
    std::vector<std::string> app_args;

    /**
     * @brief Environment variables explicitly captured for replay.
     */
    std::vector<ReplayEnvVar> env;

    /**
     * @brief Timing information.
     */
    ReplayTiming timing;

    /**
     * @brief Process result.
     */
    ReplayProcessResult process;

    /**
     * @brief Captured log file paths.
     */
    ReplayLogPaths logs;

    /**
     * @brief Short human-readable error message.
     */
    std::string error_message;

    /**
     * @brief Optional diagnostic hint.
     */
    std::string hint;

    /**
     * @brief True when this record can be replayed safely.
     */
    bool replayable{true};

    /**
     * @brief True when the original execution used watch mode.
     */
    bool watch{false};

    /**
     * @brief True when the original execution used direct script compilation.
     */
    bool direct_script{false};

    /**
     * @brief True when the original execution used generated CMake fallback.
     */
    bool cmake_fallback{false};
  };

  /**
   * @brief Return true when a replay record represents a failed execution.
   *
   * @param record Replay record.
   * @return true if the record failed, crashed, timed out, or was interrupted.
   */
  inline bool is_failed(const ReplayRecord &record)
  {
    return record.status == ReplayStatus::Failed ||
           record.status == ReplayStatus::Crashed ||
           record.status == ReplayStatus::TimedOut ||
           record.status == ReplayStatus::Interrupted;
  }

  /**
   * @brief Return true when a replay record completed successfully.
   *
   * @param record Replay record.
   * @return true when the record status is ReplayStatus::Success.
   */
  inline bool is_success(const ReplayRecord &record)
  {
    return record.status == ReplayStatus::Success;
  }

  /**
   * @brief Return true when the process result was caused by SIGINT.
   *
   * @param record Replay record.
   * @return true when the execution looks like a user interruption.
   */
  inline bool is_interrupted(const ReplayRecord &record)
  {
    return record.status == ReplayStatus::Interrupted ||
           record.process.exit_code == 130 ||
           record.process.signal == 2;
  }

} // namespace vix::commands::replay

#endif // VIX_CLI_COMMANDS_REPLAY_RECORD_HPP
