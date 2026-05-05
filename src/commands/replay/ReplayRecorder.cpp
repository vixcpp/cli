/**
 *
 *  @file ReplayRecorder.cpp
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
#include <vix/cli/commands/replay/ReplayRecorder.hpp>
#include <vix/cli/commands/replay/ReplayId.hpp>
#include <vix/cli/commands/replay/ReplayStore.hpp>

#include <fstream>
#include <system_error>

namespace vix::commands::replay
{

  namespace
  {

    /**
     * @brief Write an empty file.
     *
     * @param path Target file path.
     * @param err Error message written on failure.
     * @return true on success.
     */
    bool touch_file(const fs::path &path, std::string &err)
    {
      std::error_code ec;
      fs::create_directories(path.parent_path(), ec);

      if (ec)
      {
        err = ec.message();
        return false;
      }

      std::ofstream out(path, std::ios::binary | std::ios::app);
      if (!out)
      {
        err = "cannot create file: " + path.string();
        return false;
      }

      return true;
    }

    /**
     * @brief Append text to a file.
     *
     * @param path Target file path.
     * @param text Text to append.
     * @param err Error message written on failure.
     * @return true on success.
     */
    bool append_text_file(const fs::path &path, const std::string &text, std::string &err)
    {
      std::error_code ec;
      fs::create_directories(path.parent_path(), ec);

      if (ec)
      {
        err = ec.message();
        return false;
      }

      std::ofstream out(path, std::ios::binary | std::ios::app);
      if (!out)
      {
        err = "cannot open file for append: " + path.string();
        return false;
      }

      out.write(text.data(), static_cast<std::streamsize>(text.size()));

      if (!out.good())
      {
        err = "cannot append to file: " + path.string();
        return false;
      }

      return true;
    }

    /**
     * @brief Return true when an exit code represents a user interruption.
     *
     * @param exitCode Normalized exit code.
     * @return true when the exit code is SIGINT-like.
     */
    bool is_interrupt_exit_code(int exitCode)
    {
      return exitCode == 130;
    }

  } // namespace

  bool ReplayRecorder::begin(const ReplayRecorderConfig &config, std::string &err)
  {
    if (active_)
    {
      err = "replay recorder is already active";
      return false;
    }

    base_dir_ = config.base_dir.empty() ? fs::current_path() : config.base_dir;
    const std::string runId = make_replay_id();

    paths_ = make_replay_run_paths(base_dir_, runId);

    if (!ensure_replay_run_dir(paths_, err))
      return false;

    if (!touch_file(paths_.stdout_file, err))
      return false;

    if (!touch_file(paths_.stderr_file, err))
      return false;

    if (!touch_file(paths_.combined_file, err))
      return false;

    start_ = replay_now_start();

    record_ = ReplayRecord{};
    record_.id = runId;
    record_.schema_version = 1;
    record_.mode = config.mode;
    record_.target_kind = config.target_kind;
    record_.status = ReplayStatus::Unknown;
    record_.error_kind = ReplayErrorKind::None;

    record_.cwd = config.cwd.empty() ? fs::current_path() : config.cwd;
    record_.project_dir = config.project_dir;
    record_.target_path = config.target_path;

    record_.command = config.command;
    record_.resolved_command = config.resolved_command;

    record_.vix_args = config.vix_args;
    record_.app_args = config.app_args;
    record_.env = config.env;

    record_.timing.started_at = format_replay_timestamp_utc(start_.wall);
    record_.timing.finished_at = {};
    record_.timing.duration_ms = 0;

    record_.logs.stdout_log = paths_.stdout_file;
    record_.logs.stderr_log = paths_.stderr_file;
    record_.logs.combined_log = paths_.combined_file;

    record_.replayable = config.replayable;
    record_.watch = config.watch;
    record_.direct_script = config.direct_script;
    record_.cmake_fallback = config.cmake_fallback;

    active_ = true;
    return true;
  }

  bool ReplayRecorder::append_stdout(const std::string &text, std::string &err)
  {
    if (!active_)
    {
      err = "replay recorder is not active";
      return false;
    }

    if (!append_text_file(paths_.stdout_file, text, err))
      return false;

    return append_combined(text, err);
  }

  bool ReplayRecorder::append_stderr(const std::string &text, std::string &err)
  {
    if (!active_)
    {
      err = "replay recorder is not active";
      return false;
    }

    if (!append_text_file(paths_.stderr_file, text, err))
      return false;

    return append_combined(text, err);
  }

  bool ReplayRecorder::append_combined(const std::string &text, std::string &err)
  {
    if (!active_)
    {
      err = "replay recorder is not active";
      return false;
    }

    return append_text_file(paths_.combined_file, text, err);
  }

  bool ReplayRecorder::finish(const ReplayRecorderFinish &finishData, std::string &err)
  {
    if (!active_)
    {
      err = "replay recorder is not active";
      return false;
    }

    const ReplayFinishTime finishTime = replay_now_finish();

    record_.timing.finished_at = format_replay_timestamp_utc(finishTime.wall);
    record_.timing.duration_ms = replay_duration_ms(start_, finishTime);

    record_.status = finishData.status;
    record_.error_kind = finishData.error_kind;
    record_.process = finishData.process;
    record_.error_message = finishData.error_message;
    record_.hint = finishData.hint;

    if (record_.status == ReplayStatus::Unknown)
      record_.status = infer_replay_status_from_process(record_.process);

    if (record_.error_kind == ReplayErrorKind::Unknown ||
        record_.error_kind == ReplayErrorKind::None)
    {
      if (record_.status != ReplayStatus::Success)
        record_.error_kind = infer_replay_error_kind_from_process(record_.process);
    }

    if (!save_replay_record(base_dir_, record_, err))
      return false;

    active_ = false;
    return true;
  }

  bool ReplayRecorder::active() const
  {
    return active_;
  }

  const ReplayRecord &ReplayRecorder::record() const
  {
    return record_;
  }

  const ReplayRunPaths &ReplayRecorder::paths() const
  {
    return paths_;
  }

  const std::string &ReplayRecorder::id() const
  {
    return record_.id;
  }

  ReplayProcessResult make_replay_process_result(
      int exitCode,
      int rawStatus,
      bool terminatedBySignal,
      int signal)
  {
    ReplayProcessResult result{};

    result.exit_code = exitCode;
    result.raw_status = rawStatus;
    result.terminated_by_signal = terminatedBySignal;
    result.signal = signal;

    return result;
  }

  ReplayStatus infer_replay_status_from_process(const ReplayProcessResult &process)
  {
    if (process.terminated_by_signal)
    {
      if (process.signal == 2)
        return ReplayStatus::Interrupted;

      return ReplayStatus::Crashed;
    }

    if (is_interrupt_exit_code(process.exit_code))
      return ReplayStatus::Interrupted;

    if (process.exit_code == 0)
      return ReplayStatus::Success;

    return ReplayStatus::Failed;
  }

  ReplayErrorKind infer_replay_error_kind_from_process(const ReplayProcessResult &process)
  {
    if (process.terminated_by_signal)
      return ReplayErrorKind::Signal;

    if (is_interrupt_exit_code(process.exit_code))
      return ReplayErrorKind::Signal;

    if (process.exit_code == 0)
      return ReplayErrorKind::None;

    return ReplayErrorKind::RuntimeError;
  }

} // namespace vix::commands::replay
