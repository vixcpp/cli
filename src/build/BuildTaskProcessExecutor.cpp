/**
 *
 *  @file BuildTaskProcessExecutor.cpp
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

#include <vix/cli/build/BuildTaskProcessExecutor.hpp>

#include <vix/engine/Process.hpp>

namespace vix::cli::build
{
  BuildTaskResult execute_build_task_process(BuildTask &task)
  {
    BuildTaskResult result;
    result.taskId = task.id;

    if (task.command.empty())
    {
      result.state = BuildTaskState::Failed;
      result.exitCode = 127;
      result.output = "Empty build command for task: " + task.id + "\n";
      return result;
    }

    vix::engine::process::Command command;
    command.argv = task.command;
    command.workingDirectory = task.workingDirectory;
    command.mergeStdErr = true;

    const vix::engine::process::Result processResult =
        vix::engine::process::execute(command);

    result.exitCode = processResult.exitCode;
    result.output = processResult.output;
    if (!processResult.errorMessage.empty())
      result.output += processResult.errorMessage + "\n";

    if (processResult.success())
      result.state = BuildTaskState::Done;
    else
      result.state = BuildTaskState::Failed;

    return result;
  }

} // namespace vix::cli::build
