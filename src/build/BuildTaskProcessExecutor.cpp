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

#include <cstdio>
#include <filesystem>
#include <sstream>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  namespace
  {
    static std::string shell_quote(const std::string &value)
    {
#ifdef _WIN32
      std::string out = "\"";
      for (char c : value)
      {
        if (c == '"')
          out += "\"\"";
        else
          out.push_back(c);
      }
      out += "\"";
      return out;
#else
      std::string out = "'";
      for (char c : value)
      {
        if (c == '\'')
          out += "'\\''";
        else
          out.push_back(c);
      }
      out += "'";
      return out;
#endif
    }

    static std::string command_to_shell_string(const std::vector<std::string> &command)
    {
      std::ostringstream out;

      for (std::size_t i = 0; i < command.size(); ++i)
      {
        if (i > 0)
          out << " ";

        out << shell_quote(command[i]);
      }

      return out.str();
    }

    static std::string command_to_shell_string_with_working_directory(
        const std::vector<std::string> &command,
        const fs::path &workingDirectory)
    {
      std::ostringstream out;

#ifndef _WIN32
      if (!workingDirectory.empty())
      {
        out << "cd ";
        out << shell_quote(workingDirectory.string());
        out << " && ";
      }
#else
      if (!workingDirectory.empty())
      {
        out << "cd /d ";
        out << shell_quote(workingDirectory.string());
        out << " && ";
      }
#endif

      out << command_to_shell_string(command);
      return out.str();
    }

    static int normalize_exit_code(int code)
    {
#ifdef _WIN32
      return code;
#else
      if (WIFEXITED(code))
        return WEXITSTATUS(code);

      if (WIFSIGNALED(code))
        return 128 + WTERMSIG(code);

      return code;
#endif
    }
  } // namespace

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

    const std::string shellCommand =
        command_to_shell_string_with_working_directory(
            task.command,
            task.workingDirectory) +
        " 2>&1";

#ifdef _WIN32
    FILE *pipe = _popen(shellCommand.c_str(), "r");
#else
    FILE *pipe = popen(shellCommand.c_str(), "r");
#endif

    if (!pipe)
    {
      result.state = BuildTaskState::Failed;
      result.exitCode = 127;
      result.output = "Failed to start task: " + task.id + "\n";
      return result;
    }

    std::ostringstream output;
    char buffer[4096];

    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
      output << buffer;

#ifdef _WIN32
    const int rawCode = _pclose(pipe);
#else
    const int rawCode = pclose(pipe);
#endif

    const int exitCode = normalize_exit_code(rawCode);

    result.exitCode = exitCode;
    result.output = output.str();

    if (exitCode == 0)
      result.state = BuildTaskState::Done;
    else
      result.state = BuildTaskState::Failed;

    return result;
  }

} // namespace vix::cli::build
