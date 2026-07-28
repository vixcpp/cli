/**
 *
 *  @file BuildStyle.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Reusable build output style
 *
 */
#ifndef VIX_CLI_BUILD_BUILD_TASK_HPP
#define VIX_CLI_BUILD_BUILD_TASK_HPP

#include <vix/engine/BuildTask.hpp>

namespace vix::cli::build
{
  using vix::engine::build_task_kind_from_string;
  using vix::engine::build_task_state_from_string;
  using vix::engine::BuildTask;
  using vix::engine::BuildTaskKind;
  using vix::engine::BuildTaskState;
  using vix::engine::hash_build_command;
  using vix::engine::make_archive_task;
  using vix::engine::make_build_task_id;
  using vix::engine::make_compile_task;
  using vix::engine::make_link_task;
  using vix::engine::to_string;
} // namespace vix::cli::build

#endif
