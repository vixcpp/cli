/**
 *
 *  @file ReplayRunner.cpp
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
#include <vix/cli/commands/replay/ReplayRunner.hpp>
#include <vix/cli/commands/replay/ReplayId.hpp>
#include <vix/cli/commands/replay/ReplayPrinter.hpp>

#include <iostream>

namespace vix::commands::replay
{

  fs::path default_replay_base_dir()
  {
    return fs::current_path();
  }

  bool resolve_latest_failed_replay_record(
      const fs::path &baseDir,
      ReplayRecord &record,
      std::string &err)
  {
    err.clear();

    const auto failed = list_failed_replay_runs(baseDir, 1, err);

    if (!err.empty())
      return false;

    if (failed.empty())
    {
      err = "no failed replay run found";
      return false;
    }

    return load_replay_record(baseDir, failed.front().id, record, err);
  }

  bool resolve_replay_record(
      const fs::path &baseDir,
      const ReplaySelector &selector,
      ReplayRecord &record,
      std::string &err)
  {
    const std::string value = normalize_replay_id(selector.value.empty() ? "last" : selector.value);

    if (is_latest_selector(value))
      return load_latest_replay_record(baseDir, record, err);

    if (is_failed_selector(value))
      return resolve_latest_failed_replay_record(baseDir, record, err);

    return load_replay_record(baseDir, value, record, err);
  }

  bool run_replay(
      const ReplayRunnerOptions &options,
      ReplayRunnerResult &result,
      std::string &err)
  {
    const fs::path baseDir = options.base_dir.empty()
                                 ? default_replay_base_dir()
                                 : options.base_dir;

    ReplayRecord record{};
    if (!resolve_replay_record(baseDir, options.selector, record, err))
      return false;

    result.record = record;

    if (options.print_summary)
      print_replay_record(std::cout, record);

    ReplayProcessOptions processOptions{};
    processOptions.cwd_override = options.cwd_override;
    processOptions.extra_args = options.extra_args;
    processOptions.extra_env = options.extra_env;
    processOptions.print_command = options.print_command;
    processOptions.dry_run = options.dry_run;

    ReplayProcessRunResult processResult{};
    if (!run_replay_process(record, processOptions, processResult, err))
      return false;

    result.process = processResult;
    result.launched = true;
    result.success = processResult.exit_code == 0;

    return true;
  }

} // namespace vix::commands::replay
