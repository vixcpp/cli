/**
 *
 *  @file BuildEvent.hpp
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

#pragma once

#include <cstddef>
#include <string>

namespace vix::cli::build
{
  enum class BuildEventKind
  {
    ProjectLoadStarted,
    ProjectLoadFinished,

    ConfigureStarted,
    ConfigureFinished,

    DependencyResolutionStarted,
    DependencyResolutionFinished,

    CompileStarted,
    CompileProgress,
    CompileFinished,

    LinkStarted,
    LinkFinished,

    BuildSucceeded,
    BuildFailed
  };

  struct BuildEvent
  {
    BuildEventKind kind{BuildEventKind::ProjectLoadStarted};

    std::string target;
    std::string file;
    std::string message;

    std::size_t current{0};
    std::size_t total{0};
  };

  constexpr const char *build_event_kind_name(
      BuildEventKind kind) noexcept
  {
    switch (kind)
    {
    case BuildEventKind::ProjectLoadStarted:
      return "project_load_started";

    case BuildEventKind::ProjectLoadFinished:
      return "project_load_finished";

    case BuildEventKind::ConfigureStarted:
      return "configure_started";

    case BuildEventKind::ConfigureFinished:
      return "configure_finished";

    case BuildEventKind::DependencyResolutionStarted:
      return "dependency_resolution_started";

    case BuildEventKind::DependencyResolutionFinished:
      return "dependency_resolution_finished";

    case BuildEventKind::CompileStarted:
      return "compile_started";

    case BuildEventKind::CompileProgress:
      return "compile_progress";

    case BuildEventKind::CompileFinished:
      return "compile_finished";

    case BuildEventKind::LinkStarted:
      return "link_started";

    case BuildEventKind::LinkFinished:
      return "link_finished";

    case BuildEventKind::BuildSucceeded:
      return "build_succeeded";

    case BuildEventKind::BuildFailed:
      return "build_failed";
    }

    return "unknown";
  }

} // namespace vix::cli::build
