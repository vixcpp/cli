/**
 *
 *  @file ReplayRecorder.hpp
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
#ifndef VIX_CLI_COMMANDS_REPLAY_RECORDER_HPP
#define VIX_CLI_COMMANDS_REPLAY_RECORDER_HPP

#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/commands/replay/ReplayClock.hpp>
#include <vix/cli/commands/replay/ReplayPaths.hpp>
#include <vix/cli/commands/replay/ReplayRecord.hpp>

namespace vix::commands::replay
{

  namespace fs = std::filesystem;

  /**
   * @brief Configuration used to start a replay recording session.
   */
  struct ReplayRecorderConfig
  {
    /**
     * @brief Base directory where .vix/runs is stored.
     */
    fs::path base_dir;

    /**
     * @brief Command mode that starts the recording.
     */
    ReplayMode mode{ReplayMode::Unknown};

    /**
     * @brief Target kind executed by the command.
     */
    ReplayTargetKind target_kind{ReplayTargetKind::Unknown};

    /**
     * @brief Working directory used by the original command.
     */
    fs::path cwd;

    /**
     * @brief Project directory when known.
     */
    fs::path project_dir;

    /**
     * @brief Main target path.
     *
     * For script mode, this is the .cpp file.
     * For manifest mode, this is the .vix file.
     * For binary mode, this is the executable path.
     */
    fs::path target_path;

    /**
     * @brief Public command shown to the user.
     */
    std::string command;

    /**
     * @brief Internal resolved command, when different from command.
     */
    std::string resolved_command;

    /**
     * @brief Arguments passed to Vix.
     */
    std::vector<std::string> vix_args;

    /**
     * @brief Arguments forwarded to the application.
     */
    std::vector<std::string> app_args;

    /**
     * @brief Environment variables explicitly captured for replay.
     */
    std::vector<ReplayEnvVar> env;

    /**
     * @brief True when the original execution uses watch mode.
     */
    bool watch{false};

    /**
     * @brief True when direct script compilation is used.
     */
    bool direct_script{false};

    /**
     * @brief True when generated CMake fallback is used.
     */
    bool cmake_fallback{false};

    /**
     * @brief True when the record can be replayed safely.
     */
    bool replayable{true};
  };

  /**
   * @brief Final data used to close a replay recording session.
   */
  struct ReplayRecorderFinish
  {
    /**
     * @brief Final replay status.
     */
    ReplayStatus status{ReplayStatus::Unknown};

    /**
     * @brief Failure category.
     */
    ReplayErrorKind error_kind{ReplayErrorKind::None};

    /**
     * @brief Captured process result.
     */
    ReplayProcessResult process{};

    /**
     * @brief Short human-readable error message.
     */
    std::string error_message;

    /**
     * @brief Optional diagnostic hint.
     */
    std::string hint;
  };

  /**
   * @brief Records one replayable Vix execution.
   *
   * ReplayRecorder is used by `vix run`, `vix dev`, and later by server/runtime
   * capture code. It owns the run id, log paths, timing information, and final
   * persistence of the ReplayRecord.
   */
  class ReplayRecorder
  {
  public:
    /**
     * @brief Construct an inactive replay recorder.
     */
    ReplayRecorder() = default;

    /**
     * @brief Start a replay recording session.
     *
     * This creates:
     * - .vix/runs/<id>/
     * - stdout.log
     * - stderr.log
     * - combined.log
     *
     * @param config Recorder configuration.
     * @param err Error message written on failure.
     * @return true on success.
     */
    bool begin(const ReplayRecorderConfig &config, std::string &err);

    /**
     * @brief Append text to the captured stdout log.
     *
     * @param text Text to append.
     * @param err Error message written on failure.
     * @return true on success.
     */
    bool append_stdout(const std::string &text, std::string &err);

    /**
     * @brief Append text to the captured stderr log.
     *
     * @param text Text to append.
     * @param err Error message written on failure.
     * @return true on success.
     */
    bool append_stderr(const std::string &text, std::string &err);

    /**
     * @brief Append text to the combined output log.
     *
     * @param text Text to append.
     * @param err Error message written on failure.
     * @return true on success.
     */
    bool append_combined(const std::string &text, std::string &err);

    /**
     * @brief Finish the replay recording session and persist run.json.
     *
     * @param finish Final recording data.
     * @param err Error message written on failure.
     * @return true on success.
     */
    bool finish(const ReplayRecorderFinish &finish, std::string &err);

    /**
     * @brief Return true when recording has started and has not finished.
     *
     * @return true when the recorder is active.
     */
    bool active() const;

    /**
     * @brief Return the current replay record.
     *
     * @return Current replay record.
     */
    const ReplayRecord &record() const;

    /**
     * @brief Return all paths for the current replay run.
     *
     * @return Replay run paths.
     */
    const ReplayRunPaths &paths() const;

    /**
     * @brief Return the current replay id.
     *
     * @return Replay id.
     */
    const std::string &id() const;

  private:
    /**
     * @brief Whether the recorder is currently active.
     */
    bool active_{false};

    /**
     * @brief Current replay record.
     */
    ReplayRecord record_{};

    /**
     * @brief Current replay run paths.
     */
    ReplayRunPaths paths_{};

    /**
     * @brief Start time captured when begin() succeeds.
     */
    ReplayStartTime start_{};

    /**
     * @brief Base directory where this replay run is stored.
     */
    fs::path base_dir_{};
  };

  /**
   * @brief Build a ReplayProcessResult from raw process values.
   *
   * @param exitCode Normalized exit code.
   * @param rawStatus Raw process status.
   * @param terminatedBySignal True when the process ended by signal.
   * @param signal Signal number.
   * @return Replay process result.
   */
  ReplayProcessResult make_replay_process_result(
      int exitCode,
      int rawStatus,
      bool terminatedBySignal,
      int signal);

  /**
   * @brief Infer replay status from a process result.
   *
   * @param process Process result.
   * @return Replay status.
   */
  ReplayStatus infer_replay_status_from_process(const ReplayProcessResult &process);

  /**
   * @brief Infer replay error kind from a process result.
   *
   * @param process Process result.
   * @return Replay error kind.
   */
  ReplayErrorKind infer_replay_error_kind_from_process(const ReplayProcessResult &process);

} // namespace vix::commands::replay

#endif // VIX_CLI_COMMANDS_REPLAY_RECORDER_HPP
