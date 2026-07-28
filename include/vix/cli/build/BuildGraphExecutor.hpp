/**
 *
 *  @file BuildGraphExecutor.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Compatibility adapter for the engine graph executor
 *
 */

#ifndef VIX_CLI_BUILD_BUILD_GRAPH_EXECUTOR_HPP
#define VIX_CLI_BUILD_BUILD_GRAPH_EXECUTOR_HPP

#include <vix/engine/BuildGraphExecutor.hpp>

namespace vix::cli::build
{
  using vix::engine::BuildGraphCompileExecutor;
  using vix::engine::BuildGraphExecutor;
  using vix::engine::BuildGraphExecutorDependencies;
  using vix::engine::BuildGraphExecutorEvent;
  using vix::engine::BuildGraphExecutorEventKind;
  using vix::engine::BuildGraphExecutorEventSink;
  using vix::engine::BuildGraphExecutorNinjaRequest;
  using vix::engine::BuildGraphExecutorNinjaResult;
  using vix::engine::BuildGraphExecutorOptions;
  using vix::engine::BuildGraphExecutorResult;
  using vix::engine::BuildGraphExecutorStatus;
  using vix::engine::BuildGraphNinjaExecutor;
  using vix::engine::to_string;

} // namespace vix::cli::build

#endif
