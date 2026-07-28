/**
 *
 *  @file BuildGraphExecutorAdapter.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  CLI adapters for target-aware graph execution
 *
 */

#ifndef VIX_CLI_BUILD_BUILD_GRAPH_EXECUTOR_ADAPTER_HPP
#define VIX_CLI_BUILD_BUILD_GRAPH_EXECUTOR_ADAPTER_HPP

#include <vix/cli/build/BuildGraphExecutor.hpp>

namespace vix::cli::build
{
  BuildGraphExecutorNinjaResult execute_graph_ninja_target(
      const BuildGraphExecutorNinjaRequest &request,
      bool quiet);

  void render_graph_debug_event(
      const BuildGraphExecutorEvent &event,
      bool quiet,
      bool verbose);

} // namespace vix::cli::build

#endif
