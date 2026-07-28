/**
 *
 *  @file BuildScheduler.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Compatibility adapter for the engine build scheduler.
 *
 */

#ifndef VIX_CLI_BUILD_BUILD_SCHEDULER_HPP
#define VIX_CLI_BUILD_BUILD_SCHEDULER_HPP

#include <vix/engine/BuildScheduler.hpp>

namespace vix::cli::build
{
  using vix::engine::BuildScheduler;
  using vix::engine::BuildSchedulerOptions;
  using vix::engine::BuildSchedulerResult;
  using vix::engine::BuildTask;
  using vix::engine::BuildTaskExecutor;
  using vix::engine::BuildTaskKind;
  using vix::engine::BuildTaskResult;
  using vix::engine::BuildTaskState;

} // namespace vix::cli::build

#endif
