/**
 *
 *  @file BuildLiveRenderer.cpp
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

#include <vix/cli/build/BuildLiveRenderer.hpp>

#include <iostream>
#include <ostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace vix::cli::build
{
  namespace
  {
    constexpr const char *indent =
        "  ";

    constexpr const char *clearLine =
        "\r\033[2K";

  } // anonymous namespace

  BuildLiveRenderer::BuildLiveRenderer(
      std::ostream &output)
      : output_(output),
        interactive_(
            detect_interactive_output())
  {
  }

  void BuildLiveRenderer::render(
      const BuildEvent &event)
  {
    switch (event.kind)
    {
    case BuildEventKind::ProjectLoadStarted:
    case BuildEventKind::ProjectLoadFinished:
      render_project_event(event);
      break;

    case BuildEventKind::ConfigureStarted:
    case BuildEventKind::ConfigureFinished:
      render_configure_event(event);
      break;

    case BuildEventKind::DependencyResolutionStarted:
    case BuildEventKind::DependencyResolutionFinished:
      render_dependency_event(event);
      break;

    case BuildEventKind::CompileStarted:
    case BuildEventKind::CompileProgress:
    case BuildEventKind::CompileFinished:
      render_compile_event(event);
      break;

    case BuildEventKind::LinkStarted:
    case BuildEventKind::LinkFinished:
      render_link_event(event);
      break;

    case BuildEventKind::BuildSucceeded:
    case BuildEventKind::BuildFailed:
      render_result_event(event);
      break;
    }

    output_.flush();
  }

  void BuildLiveRenderer::reset() noexcept
  {
    end_live_line();

    projectShown_ = false;
    configureShown_ = false;
    dependenciesShown_ = false;
    compileShown_ = false;
    linkShown_ = false;
  }

  bool BuildLiveRenderer::interactive() const noexcept
  {
    return interactive_;
  }

  std::string BuildLiveRenderer::progress_text(
      std::size_t current,
      std::size_t total) const
  {
    if (total == 0)
      return {};

    std::ostringstream stream;

    stream
        << current
        << " / "
        << total;

    return stream.str();
  }

  void BuildLiveRenderer::write_live_line(
      const std::string &text)
  {
    if (!interactive_)
    {
      output_
          << text
          << '\n';

      return;
    }

    output_
        << clearLine
        << text;

    liveLineActive_ = true;
  }

  void BuildLiveRenderer::finish_live_line(
      const std::string &text)
  {
    if (interactive_)
    {
      if (liveLineActive_)
      {
        output_
            << clearLine;
      }

      output_
          << text
          << '\n';

      liveLineActive_ = false;
      return;
    }

    output_
        << text
        << '\n';
  }

  void BuildLiveRenderer::end_live_line()
  {
    if (!liveLineActive_)
      return;

    if (interactive_)
      output_ << '\n';

    liveLineActive_ = false;
  }

  void BuildLiveRenderer::clear_live_line()
  {
    if (!interactive_ ||
        !liveLineActive_)
    {
      return;
    }

    output_
        << clearLine;

    liveLineActive_ = false;
  }

  bool BuildLiveRenderer::
      detect_interactive_output() const noexcept
  {
    int fd = -1;

    if (&output_ == &std::cout)
    {
#ifdef _WIN32
      fd = _fileno(stdout);
#else
      fd = STDOUT_FILENO;
#endif
    }
    else if (&output_ == &std::cerr ||
             &output_ == &std::clog)
    {
#ifdef _WIN32
      fd = _fileno(stderr);
#else
      fd = STDERR_FILENO;
#endif
    }
    else
    {
      return false;
    }

    if (fd < 0)
      return false;

#ifdef _WIN32
    return _isatty(fd) != 0;
#else
    return ::isatty(fd) != 0;
#endif
  }

  void BuildLiveRenderer::render_project_event(
      const BuildEvent &event)
  {
    if (event.kind ==
        BuildEventKind::ProjectLoadStarted)
    {
      if (projectShown_)
        return;

      write_live_line(
          std::string(indent) +
          "→ Reading project");

      projectShown_ = true;
      return;
    }

    std::string line =
        std::string(indent) +
        "✓ Project ready";

    if (!event.target.empty())
    {
      line += "  ";
      line += event.target;
    }

    finish_live_line(line);
  }

  void BuildLiveRenderer::render_configure_event(
      const BuildEvent &event)
  {
    if (event.kind ==
        BuildEventKind::ConfigureStarted)
    {
      if (configureShown_)
        return;

      write_live_line(
          std::string(indent) +
          "→ Configuring build");

      configureShown_ = true;
      return;
    }

    finish_live_line(
        std::string(indent) +
        "✓ Build configured");
  }

  void BuildLiveRenderer::render_dependency_event(
      const BuildEvent &event)
  {
    if (event.kind ==
        BuildEventKind::DependencyResolutionStarted)
    {
      if (dependenciesShown_)
        return;

      write_live_line(
          std::string(indent) +
          "→ Resolving dependencies");

      dependenciesShown_ = true;
      return;
    }

    std::string line =
        std::string(indent) +
        "✓ Dependencies ready";

    if (!event.message.empty())
    {
      line += "  ";
      line += event.message;
    }

    finish_live_line(line);
  }

  void BuildLiveRenderer::render_compile_event(
      const BuildEvent &event)
  {
    if (event.kind ==
        BuildEventKind::CompileStarted)
    {
      compileShown_ = true;

      std::string line =
          std::string(indent) +
          "→ Compiling";

      const std::string progress =
          progress_text(
              event.current,
              event.total);

      if (!progress.empty())
      {
        line += "  ";
        line += progress;
      }

      if (!event.file.empty())
      {
        line += "  ";
        line += event.file;
      }

      write_live_line(line);
      return;
    }

    if (event.kind ==
        BuildEventKind::CompileProgress)
    {
      compileShown_ = true;

      std::string line =
          std::string(indent) +
          "→ Compiling";

      const std::string progress =
          progress_text(
              event.current,
              event.total);

      if (!progress.empty())
      {
        line += "  ";
        line += progress;
      }

      if (!event.file.empty())
      {
        line += "  ";
        line += event.file;
      }

      write_live_line(line);
      return;
    }

    std::string line =
        std::string(indent) +
        "✓ Compilation finished";

    const std::string progress =
        progress_text(
            event.current,
            event.total);

    if (!progress.empty())
    {
      line += "  ";
      line += progress;
    }

    finish_live_line(line);
  }

  void BuildLiveRenderer::render_link_event(
      const BuildEvent &event)
  {
    if (event.kind ==
        BuildEventKind::LinkStarted)
    {
      if (linkShown_)
        return;

      std::string line =
          std::string(indent) +
          "→ Linking";

      if (!event.target.empty())
      {
        line += "  ";
        line += event.target;
      }

      write_live_line(line);

      linkShown_ = true;
      return;
    }

    std::string line =
        std::string(indent) +
        "✓ Linked";

    if (!event.target.empty())
    {
      line += "  ";
      line += event.target;
    }

    finish_live_line(line);
  }

  void BuildLiveRenderer::render_result_event(
      const BuildEvent &event)
  {
    /*
     * A final result must never share the same terminal row with a
     * transient compile/link status.
     */
    clear_live_line();

    if (event.kind ==
        BuildEventKind::BuildSucceeded)
    {
      std::string line =
          std::string(indent) +
          "✓ Build completed";

      if (!event.target.empty())
      {
        line += "  ";
        line += event.target;
      }

      output_
          << line
          << '\n';

      return;
    }

    std::string line =
        std::string(indent) +
        "✗ ";

    if (!event.message.empty())
    {
      line += event.message;
    }
    else
    {
      line += "Build failed";
    }

    output_
        << line
        << '\n';
  }

} // namespace vix::cli::build
