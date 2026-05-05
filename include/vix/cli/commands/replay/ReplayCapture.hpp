/**
 *
 *  @file ReplayCapture.hpp
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
#ifndef VIX_CLI_COMMANDS_REPLAY_CAPTURE_HPP
#define VIX_CLI_COMMANDS_REPLAY_CAPTURE_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/commands/replay/ReplayRecorder.hpp>
#include <vix/cli/commands/replay/ReplayRecord.hpp>

namespace vix::commands::replay
{

  namespace fs = std::filesystem;

  /**
   * @brief Captured output produced by one process execution.
   */
  struct ReplayCapturedOutput
  {
    /**
     * @brief Captured stdout text.
     */
    std::string stdout_text;

    /**
     * @brief Captured stderr text.
     */
    std::string stderr_text;

    /**
     * @brief Combined stdout and stderr text.
     */
    std::string combined_text;
  };

  /**
   * @brief Normalized execution result captured for replay.
   */
  struct ReplayCapturedResult
  {
    /**
     * @brief Captured output.
     */
    ReplayCapturedOutput output{};

    /**
     * @brief Captured process result.
     */
    ReplayProcessResult process{};

    /**
     * @brief Inferred replay status.
     */
    ReplayStatus status{ReplayStatus::Unknown};

    /**
     * @brief Inferred replay error kind.
     */
    ReplayErrorKind error_kind{ReplayErrorKind::None};

    /**
     * @brief Short error message when available.
     */
    std::string error_message;

    /**
     * @brief Optional hint shown to the user.
     */
    std::string hint;
  };

  /**
   * @brief Options controlling capture behavior.
   */
  struct ReplayCaptureOptions
  {
    /**
     * @brief Maximum amount of stdout text kept in memory.
     *
     * The recorder may still write logs to disk, but memory is capped.
     */
    std::size_t max_stdout_bytes{256 * 1024};

    /**
     * @brief Maximum amount of stderr text kept in memory.
     *
     * The recorder may still write logs to disk, but memory is capped.
     */
    std::size_t max_stderr_bytes{256 * 1024};

    /**
     * @brief Maximum amount of combined output kept in memory.
     */
    std::size_t max_combined_bytes{512 * 1024};

    /**
     * @brief True when stdout chunks should be written to the recorder.
     */
    bool write_stdout{true};

    /**
     * @brief True when stderr chunks should be written to the recorder.
     */
    bool write_stderr{true};

    /**
     * @brief True when failed recorder writes should be ignored.
     */
    bool ignore_recorder_errors{true};
  };

  /**
   * @brief Stateful helper that mirrors process output into a ReplayRecorder.
   *
   * ReplayCapture is intentionally small. It does not execute a process by
   * itself. It receives stdout/stderr chunks from existing run logic and writes
   * them to the replay logs while keeping bounded in-memory excerpts.
   */
  class ReplayCapture
  {
  public:
    /**
     * @brief Construct an inactive capture.
     */
    ReplayCapture() = default;

    /**
     * @brief Construct a capture bound to a recorder.
     *
     * @param recorder Replay recorder.
     * @param options Capture options.
     */
    explicit ReplayCapture(
        ReplayRecorder *recorder,
        ReplayCaptureOptions options = {});

    /**
     * @brief Attach this capture to a recorder.
     *
     * @param recorder Replay recorder.
     * @param options Capture options.
     */
    void attach(
        ReplayRecorder *recorder,
        ReplayCaptureOptions options = {});

    /**
     * @brief Capture one stdout chunk.
     *
     * @param text Output text.
     * @param err Error message written on failure.
     * @return true on success.
     */
    bool capture_stdout(const std::string &text, std::string &err);

    /**
     * @brief Capture one stderr chunk.
     *
     * @param text Error output text.
     * @param err Error message written on failure.
     * @return true on success.
     */
    bool capture_stderr(const std::string &text, std::string &err);

    /**
     * @brief Capture a stdout chunk and ignore recorder errors.
     *
     * @param text Output text.
     */
    void capture_stdout_noexcept(const std::string &text);

    /**
     * @brief Capture a stderr chunk and ignore recorder errors.
     *
     * @param text Error output text.
     */
    void capture_stderr_noexcept(const std::string &text);

    /**
     * @brief Build the current captured output.
     *
     * @return Captured output.
     */
    ReplayCapturedOutput output() const;

    /**
     * @brief Return true when this capture has a recorder attached.
     *
     * @return true when active.
     */
    bool active() const;

    /**
     * @brief Return the last recorder error.
     *
     * @return Last recorder error message.
     */
    const std::string &last_error() const;

  private:
    /**
     * @brief Append text to a bounded string buffer.
     *
     * @param buffer Target buffer.
     * @param text Text to append.
     * @param maxBytes Maximum buffer size.
     */
    static void append_bounded(
        std::string &buffer,
        const std::string &text,
        std::size_t maxBytes);

    /**
     * @brief Attached replay recorder.
     */
    ReplayRecorder *recorder_{nullptr};

    /**
     * @brief Capture options.
     */
    ReplayCaptureOptions options_{};

    /**
     * @brief Bounded stdout memory buffer.
     */
    std::string stdout_buffer_{};

    /**
     * @brief Bounded stderr memory buffer.
     */
    std::string stderr_buffer_{};

    /**
     * @brief Bounded combined memory buffer.
     */
    std::string combined_buffer_{};

    /**
     * @brief Last recorder error.
     */
    std::string last_error_{};
  };

  /**
   * @brief Build a captured result from output and process values.
   *
   * @param output Captured output.
   * @param process Process result.
   * @return Captured result with inferred status and error kind.
   */
  ReplayCapturedResult make_replay_captured_result(
      const ReplayCapturedOutput &output,
      const ReplayProcessResult &process);

  /**
   * @brief Build recorder finish data from a captured result.
   *
   * @param result Captured result.
   * @return Recorder finish data.
   */
  ReplayRecorderFinish make_replay_finish_from_capture(
      const ReplayCapturedResult &result);

  /**
   * @brief Extract a short error message from captured output.
   *
   * @param output Captured output.
   * @return Short error message, or empty string.
   */
  std::string replay_error_message_from_output(const ReplayCapturedOutput &output);

  /**
   * @brief Extract a short replay hint from captured output.
   *
   * @param output Captured output.
   * @param process Process result.
   * @return Hint string, or empty string.
   */
  std::string replay_hint_from_output(
      const ReplayCapturedOutput &output,
      const ReplayProcessResult &process);

  /**
   * @brief Return the first non-empty line from a text block.
   *
   * @param text Text block.
   * @return First non-empty line, or empty string.
   */
  std::string first_non_empty_line(const std::string &text);

  /**
   * @brief Return the last non-empty line from a text block.
   *
   * @param text Text block.
   * @return Last non-empty line, or empty string.
   */
  std::string last_non_empty_line(const std::string &text);

} // namespace vix::commands::replay

#endif // VIX_CLI_COMMANDS_REPLAY_CAPTURE_HPP
