/**
 *
 *  @file ReplayProcess.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/replay/ReplayProcess.hpp>
#include <vix/cli/Style.hpp>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <system_error>

#ifndef _WIN32
#include <sys/wait.h>
#endif

using namespace vix::cli::style;

namespace vix::commands::replay
{

  namespace
  {

    /**
     * @brief Join extra arguments for shell execution.
     *
     * @param args Argument list.
     * @return Shell argument string.
     */
    std::string join_extra_args(const std::vector<std::string> &args)
    {
      std::string out;

      for (const auto &arg : args)
      {
        out += " ";
        out += replay_shell_quote(arg);
      }

      return out;
    }

    /**
     * @brief Build environment assignments for POSIX shells.
     *
     * @param env Environment variables.
     * @return Shell prefix.
     */
    std::string build_posix_env_prefix(const std::vector<ReplayEnvVar> &env)
    {
      std::string out;

      for (const auto &item : env)
      {
        if (item.name.empty())
          continue;

        out += item.name;
        out += "=";
        out += replay_shell_quote(item.value);
        out += " ";
      }

      return out;
    }

#ifdef _WIN32
    /**
     * @brief Build environment assignments for cmd.exe.
     *
     * @param env Environment variables.
     * @return cmd.exe prefix.
     */
    std::string build_windows_env_prefix(const std::vector<ReplayEnvVar> &env)
    {
      std::string out;

      for (const auto &item : env)
      {
        if (item.name.empty())
          continue;

        out += "set ";
        out += item.name;
        out += "=";
        out += item.value;
        out += " && ";
      }

      return out;
    }
#endif

  } // namespace

  bool can_replay_process(const ReplayRecord &record, std::string &err)
  {
    if (!record.replayable)
    {
      err = "record is marked as not replayable";
      return false;
    }

    if (record.resolved_command.empty() && record.command.empty())
    {
      err = "record has no command to replay";
      return false;
    }

    if (record.cwd.empty())
    {
      err = "record has no working directory";
      return false;
    }

    std::error_code ec;
    if (!fs::exists(record.cwd, ec) || ec)
    {
      err = "recorded working directory does not exist: " + record.cwd.string();
      return false;
    }

    return true;
  }

  std::string replay_shell_quote(const std::string &value)
  {
#ifdef _WIN32
    std::string out = "\"";

    for (char c : value)
    {
      if (c == '"')
        out += "\\\"";
      else
        out += c;
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
        out += c;
    }

    out += "'";
    return out;
#endif
  }

  std::string build_replay_process_command(
      const ReplayRecord &record,
      const ReplayProcessOptions &options)
  {
    std::string cmd = !record.resolved_command.empty()
                          ? record.resolved_command
                          : record.command;

    cmd += join_extra_args(options.extra_args);

#ifdef _WIN32
    const std::string envPrefix = build_windows_env_prefix(record.env) +
                                  build_windows_env_prefix(options.extra_env);

    if (!envPrefix.empty())
      cmd = envPrefix + cmd;
#else
    const std::string envPrefix = build_posix_env_prefix(record.env) +
                                  build_posix_env_prefix(options.extra_env);

    if (!envPrefix.empty())
      cmd = envPrefix + cmd;
#endif

    return cmd;
  }

  int replay_normalize_exit_code(int status)
  {
#ifdef _WIN32
    return status;
#else
    if (status < 0)
      return 1;

    if (status <= 255)
      return status;

    if (WIFEXITED(status))
      return WEXITSTATUS(status);

    if (WIFSIGNALED(status))
      return 128 + WTERMSIG(status);

    return 1;
#endif
  }

  bool run_replay_process(
      const ReplayRecord &record,
      const ReplayProcessOptions &options,
      ReplayProcessRunResult &result,
      std::string &err)
  {
    if (!can_replay_process(record, err))
      return false;

    result.cwd = options.cwd_override.empty()
                     ? record.cwd
                     : options.cwd_override;

    std::error_code ec;
    if (!fs::exists(result.cwd, ec) || ec)
    {
      err = "replay working directory does not exist: " + result.cwd.string();
      return false;
    }

    result.command = build_replay_process_command(record, options);

#ifdef _WIN32
    std::ostringstream shell;
    shell << "cd /D " << replay_shell_quote(result.cwd.string())
          << " && "
          << result.command;

    const std::string finalCmd = shell.str();
#else
    std::ostringstream shell;
    shell << "cd " << replay_shell_quote(result.cwd.string())
          << " && "
          << result.command;

    const std::string finalCmd = shell.str();
#endif

    if (options.print_command)
    {
      std::cout << PAD << GRAY << "replay" << RESET << "\n";
      std::cout << PAD << CYAN << BOLD << finalCmd << RESET << "\n";
    }

    if (options.dry_run)
    {
      result.raw_status = 0;
      result.exit_code = 0;
      return true;
    }

    const int raw = std::system(finalCmd.c_str());

    result.raw_status = raw;
    result.exit_code = replay_normalize_exit_code(raw);

    return true;
  }

} // namespace vix::commands::replay
