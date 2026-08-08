/**
 *
 *  @file BuildLiveProcess.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */

#include <vix/cli/build/BuildLiveProcess.hpp>

#include <mutex>
#include <ostream>
#include <string>
#include <string_view>

namespace vix::cli::build
{
  BuildLiveProcess::BuildLiveProcess(
      std::ostream &output)
      : session_(output)
  {
  }

  void BuildLiveProcess::reset()
  {
    std::lock_guard<std::mutex> lock(
        mutex_);

    session_.reset();

    target_.clear();

    started_ = false;
    finished_ = false;
  }

  void BuildLiveProcess::begin(
      std::string_view target)
  {
    std::lock_guard<std::mutex> lock(
        mutex_);

    begin_unlocked(
        target);
  }

  void BuildLiveProcess::begin_configure()
  {
    std::lock_guard<std::mutex> lock(
        mutex_);

    if (finished_)
      return;

    begin_unlocked();

    session_.configure_started();
  }

  void BuildLiveProcess::begin_dependencies()
  {
    std::lock_guard<std::mutex> lock(
        mutex_);

    if (finished_)
      return;

    begin_unlocked();

    session_.dependencies_started();
  }

  void BuildLiveProcess::end_dependencies(
      std::string_view message)
  {
    std::lock_guard<std::mutex> lock(
        mutex_);

    if (finished_)
      return;

    begin_unlocked();

    session_.dependencies_finished(
        message);
  }

  void BuildLiveProcess::compile_progress(
      std::size_t current,
      std::size_t total,
      std::string_view file,
      std::string_view target)
  {
    std::lock_guard<std::mutex> lock(
        mutex_);

    if (finished_)
      return;

    begin_unlocked(
        target);

    session_.compile_progress(
        current,
        total,
        file,
        resolve_target_unlocked(target));
  }

  void BuildLiveProcess::compile_finished(
      std::string_view target)
  {
    std::lock_guard<std::mutex> lock(
        mutex_);

    if (finished_)
      return;

    begin_unlocked(
        target);

    session_.compile_finished(
        resolve_target_unlocked(target));
  }

  void BuildLiveProcess::link_started(
      std::string_view target)
  {
    std::lock_guard<std::mutex> lock(
        mutex_);

    if (finished_)
      return;

    begin_unlocked(
        target);

    session_.link_started(
        resolve_target_unlocked(target));
  }

  void BuildLiveProcess::link_finished(
      std::string_view target)
  {
    std::lock_guard<std::mutex> lock(
        mutex_);

    if (finished_)
      return;

    begin_unlocked(
        target);

    session_.link_finished(
        resolve_target_unlocked(target));
  }

  BuildLiveProcess::OutputObserver
  BuildLiveProcess::observer()
  {
    return [this](
               std::string_view chunk)
    {
      std::lock_guard<std::mutex> lock(
          mutex_);

      if (finished_ ||
          chunk.empty())
      {
        return;
      }

      begin_unlocked();

      session_.consume_output_chunk(
          chunk);
    };
  }

  void BuildLiveProcess::finish(
      int exitCode)
  {
    std::lock_guard<std::mutex> lock(
        mutex_);

    if (finished_)
      return;

    begin_unlocked();

    session_.finish(
        exitCode,
        target_);

    finished_ = true;
  }

  const std::string &
  BuildLiveProcess::log() const noexcept
  {
    return session_.captured_log();
  }

  const std::string &
  BuildLiveProcess::target() const noexcept
  {
    return target_;
  }

  int BuildLiveProcess::exit_code() const noexcept
  {
    return session_.exit_code();
  }

  bool BuildLiveProcess::failed() const noexcept
  {
    return session_.failed();
  }

  void BuildLiveProcess::begin_unlocked(
      std::string_view target)
  {
    if (finished_)
      return;

    if (!target.empty() &&
        target_.empty())
    {
      target_ =
          std::string(target);
    }

    if (started_)
      return;

    session_.project_started(
        target_);

    session_.project_finished(
        target_);

    started_ = true;
  }

  std::string_view
  BuildLiveProcess::resolve_target_unlocked(
      std::string_view target) const noexcept
  {
    if (!target.empty())
      return target;

    return target_;
  }

} // namespace vix::cli::build
