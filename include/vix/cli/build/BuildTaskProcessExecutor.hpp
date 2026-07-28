/**
 *
 *  @file BuildTaskProcessExecutor.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  CLI process executor for build tasks
 *
 */

#ifndef VIX_CLI_BUILD_BUILD_TASK_PROCESS_EXECUTOR_HPP
#define VIX_CLI_BUILD_BUILD_TASK_PROCESS_EXECUTOR_HPP

#include <vix/cli/build/BuildScheduler.hpp>

namespace vix::cli::build
{
  /**
   * @brief Execute one build task command through the CLI process layer.
   *
   * The engine scheduler owns dependency ordering only. The CLI owns command
   * shell conversion, working-directory handling, process capture, and exit
   * status normalization until the process module is extracted separately.
   *
   * @param task Build task to execute
   * @return Captured task result
   */
  BuildTaskResult execute_build_task_process(BuildTask &task);

} // namespace vix::cli::build

#endif
