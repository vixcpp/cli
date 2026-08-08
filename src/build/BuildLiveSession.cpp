/**
 *
 *  @file BuildLiveSession.cpp
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

#include <vix/cli/build/BuildLiveSession.hpp>

#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vix::cli::build
{
  BuildLiveSession::BuildLiveSession(
      std::ostream &output)
      : renderer_(output)
  {
  }

  void BuildLiveSession::reset()
  {
    parser_.reset();
    renderer_.reset();

    capturedLog_.clear();
    pendingOutput_.clear();

    lastTarget_.clear();

    stage_ = Stage::None;

    exitCode_ = 0;
    finished_ = false;

    compileStarted_ = false;
    compileFinished_ = false;

    compileCurrent_ = 0;
    compileTotal_ = 0;

    linkStarted_ = false;
    linkFinished_ = false;
  }

  void BuildLiveSession::project_started(
      std::string_view target)
  {
    if (finished_)
      return;

    stage_ = Stage::Project;

    BuildEvent event;

    event.kind =
        BuildEventKind::ProjectLoadStarted;

    event.target =
        std::string(target);

    dispatch(event);
  }

  void BuildLiveSession::project_finished(
      std::string_view target)
  {
    if (finished_)
      return;

    BuildEvent event;

    event.kind =
        BuildEventKind::ProjectLoadFinished;

    event.target =
        std::string(target);

    dispatch(event);
  }

  void BuildLiveSession::configure_started()
  {
    if (finished_)
      return;

    stage_ = Stage::Configure;

    BuildEvent event;

    event.kind =
        BuildEventKind::ConfigureStarted;

    dispatch(event);
  }

  void BuildLiveSession::dependencies_started()
  {
    if (finished_)
      return;

    stage_ = Stage::Dependencies;

    BuildEvent event;

    event.kind =
        BuildEventKind::DependencyResolutionStarted;

    dispatch(event);
  }

  void BuildLiveSession::dependencies_finished(
      std::string_view message)
  {
    if (finished_)
      return;

    BuildEvent event;

    event.kind =
        BuildEventKind::DependencyResolutionFinished;

    event.message =
        std::string(message);

    dispatch(event);
  }

  void BuildLiveSession::compile_started(
      std::size_t current,
      std::size_t total,
      std::string_view file,
      std::string_view target)
  {
    if (finished_ ||
        compileStarted_)
    {
      return;
    }

    BuildEvent event;

    event.kind =
        BuildEventKind::CompileStarted;

    event.current = current;
    event.total = total;

    event.file =
        std::string(file);

    event.target =
        std::string(target);

    event.message =
        "Compilation started";

    dispatch(event);
  }

  void BuildLiveSession::compile_progress(
      std::size_t current,
      std::size_t total,
      std::string_view file,
      std::string_view target)
  {
    if (finished_)
      return;

    if (!compileStarted_)
    {
      compile_started(
          current,
          total,
          file,
          target);

      return;
    }

    BuildEvent event;

    event.kind =
        BuildEventKind::CompileProgress;

    event.current = current;
    event.total = total;

    event.file =
        std::string(file);

    event.target =
        std::string(target);

    event.message =
        "Compiling";

    dispatch(event);
  }

  void BuildLiveSession::compile_finished(
      std::string_view target)
  {
    if (finished_ ||
        !compileStarted_ ||
        compileFinished_)
    {
      return;
    }

    BuildEvent event;

    event.kind =
        BuildEventKind::CompileFinished;

    event.current =
        compileCurrent_;

    event.total =
        compileTotal_;

    event.target =
        target.empty()
            ? lastTarget_
            : std::string(target);

    event.message =
        "Compilation completed";

    dispatch(event);
  }

  void BuildLiveSession::link_started(
      std::string_view target)
  {
    if (finished_ ||
        linkStarted_)
    {
      return;
    }

    if (compileStarted_ &&
        !compileFinished_)
    {
      compile_finished(
          target);
    }

    BuildEvent event;

    event.kind =
        BuildEventKind::LinkStarted;

    event.target =
        target.empty()
            ? lastTarget_
            : std::string(target);

    event.message =
        "Linking started";

    dispatch(event);
  }

  void BuildLiveSession::link_finished(
      std::string_view target)
  {
    if (finished_ ||
        !linkStarted_ ||
        linkFinished_)
    {
      return;
    }

    BuildEvent event;

    event.kind =
        BuildEventKind::LinkFinished;

    event.target =
        target.empty()
            ? lastTarget_
            : std::string(target);

    event.message =
        "Linking completed";

    dispatch(event);
  }

  void BuildLiveSession::consume_stdout_line(
      std::string_view line)
  {
    consume_process_line(
        line);
  }

  void BuildLiveSession::consume_stderr_line(
      std::string_view line)
  {
    consume_process_line(
        line);
  }

  void BuildLiveSession::consume_output_chunk(
      std::string_view chunk)
  {
    if (finished_ ||
        chunk.empty())
    {
      return;
    }

    pendingOutput_.append(
        chunk.data(),
        chunk.size());

    while (true)
    {
      const std::size_t newline =
          pendingOutput_.find('\n');

      if (newline ==
          std::string::npos)
      {
        break;
      }

      std::string line =
          pendingOutput_.substr(
              0,
              newline);

      pendingOutput_.erase(
          0,
          newline + 1);

      if (!line.empty() &&
          line.back() == '\r')
      {
        line.pop_back();
      }

      consume_process_line(
          line);
    }
  }

  void BuildLiveSession::finish(
      int exitCode,
      std::string_view target)
  {
    if (finished_)
      return;

    /*
     * A process may terminate without writing a final newline.
     * Preserve and parse that final fragment before the session is
     * finalized.
     */
    flush_pending_output();

    exitCode_ = exitCode;

    if (!target.empty())
    {
      lastTarget_ =
          std::string(target);
    }

    if (exitCode_ == 0)
    {
      /*
       * Successful process termination is the authoritative completion
       * boundary for phases whose underlying tools do not emit explicit
       * finished events.
       *
       * finished_ intentionally remains false while these semantic
       * completion methods run.
       */
      if (compileStarted_ &&
          !compileFinished_)
      {
        compile_finished(
            lastTarget_);
      }

      if (linkStarted_ &&
          !linkFinished_)
      {
        link_finished(
            lastTarget_);
      }

      BuildEvent event;

      event.kind =
          BuildEventKind::BuildSucceeded;

      event.target =
          lastTarget_;

      dispatch(event);

      finished_ = true;
      return;
    }

    BuildEvent event;

    event.kind =
        BuildEventKind::BuildFailed;

    event.target =
        lastTarget_;

    switch (stage_)
    {
    case Stage::Configure:
      event.message =
          "Configuration failed";
      break;

    case Stage::Dependencies:
      event.message =
          "Dependency resolution failed";
      break;

    case Stage::Compile:
      event.message =
          "Compilation failed";
      break;

    case Stage::Link:
      event.message =
          "Linking failed";
      break;

    case Stage::Project:
      event.message =
          "Project preparation failed";
      break;

    case Stage::None:
    default:
      event.message =
          "Build failed";
      break;
    }

    dispatch(event);

    finished_ = true;
  }

  const std::string &
  BuildLiveSession::captured_log() const noexcept
  {
    return capturedLog_;
  }

  bool BuildLiveSession::failed() const noexcept
  {
    return finished_ &&
           exitCode_ != 0;
  }

  int BuildLiveSession::exit_code() const noexcept
  {
    return exitCode_;
  }

  BuildLiveSession::Stage
  BuildLiveSession::stage() const noexcept
  {
    return stage_;
  }

  void BuildLiveSession::dispatch(
      const BuildEvent &event)
  {
    if (!event.target.empty())
    {
      lastTarget_ =
          event.target;
    }

    switch (event.kind)
    {
    case BuildEventKind::ProjectLoadStarted:
    case BuildEventKind::ProjectLoadFinished:
      stage_ = Stage::Project;
      break;

    case BuildEventKind::ConfigureStarted:
    case BuildEventKind::ConfigureFinished:
      stage_ = Stage::Configure;
      break;

    case BuildEventKind::DependencyResolutionStarted:
    case BuildEventKind::DependencyResolutionFinished:
      stage_ = Stage::Dependencies;
      break;

    case BuildEventKind::CompileStarted:
      stage_ = Stage::Compile;

      compileStarted_ = true;

      compileCurrent_ =
          event.current;

      compileTotal_ =
          event.total;

      break;

    case BuildEventKind::CompileProgress:
      stage_ = Stage::Compile;

      compileCurrent_ =
          event.current;

      if (event.total > 0)
      {
        compileTotal_ =
            event.total;
      }

      break;

    case BuildEventKind::CompileFinished:
      stage_ = Stage::Compile;

      compileFinished_ = true;

      if (event.current > 0)
      {
        compileCurrent_ =
            event.current;
      }

      if (event.total > 0)
      {
        compileTotal_ =
            event.total;
      }

      break;

    case BuildEventKind::LinkStarted:
      stage_ = Stage::Link;
      linkStarted_ = true;
      break;

    case BuildEventKind::LinkFinished:
      stage_ = Stage::Link;
      linkFinished_ = true;
      break;

    case BuildEventKind::BuildSucceeded:
    case BuildEventKind::BuildFailed:
      break;
    }

    renderer_.render(
        event);
  }

  void BuildLiveSession::consume_process_line(
      std::string_view line)
  {
    if (finished_)
      return;

    capturedLog_.append(
        line.data(),
        line.size());

    capturedLog_.push_back('\n');

    const std::vector<BuildEvent> events =
        parser_.consume(
            line);

    for (const BuildEvent &event : events)
    {
      dispatch(
          event);
    }
  }

  void BuildLiveSession::flush_pending_output()
  {
    if (pendingOutput_.empty())
      return;

    std::string line =
        std::move(pendingOutput_);

    pendingOutput_.clear();

    if (!line.empty() &&
        line.back() == '\r')
    {
      line.pop_back();
    }

    if (!line.empty())
    {
      consume_process_line(
          line);
    }
  }

} // namespace vix::cli::build
