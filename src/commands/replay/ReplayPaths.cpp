/**
 *
 *  @file ReplayPaths.cpp
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
#include <vix/cli/commands/replay/ReplayPaths.hpp>

#include <cctype>
#include <system_error>

namespace vix::commands::replay
{

  fs::path replay_vix_dir(const fs::path &baseDir)
  {
    return baseDir / ".vix";
  }

  fs::path replay_runs_root(const fs::path &baseDir)
  {
    return replay_vix_dir(baseDir) / "runs";
  }

  fs::path replay_latest_file(const fs::path &baseDir)
  {
    return replay_runs_root(baseDir) / "latest";
  }

  fs::path replay_run_dir(const fs::path &baseDir, const std::string &id)
  {
    return replay_runs_root(baseDir) / id;
  }

  ReplayRunPaths make_replay_run_paths(const fs::path &baseDir, const std::string &id)
  {
    ReplayRunPaths paths{};

    paths.id = id;
    paths.runs_root = replay_runs_root(baseDir);
    paths.run_dir = paths.runs_root / id;
    paths.record_file = paths.run_dir / "run.json";
    paths.stdout_file = paths.run_dir / "stdout.log";
    paths.stderr_file = paths.run_dir / "stderr.log";
    paths.combined_file = paths.run_dir / "combined.log";
    paths.latest_file = paths.runs_root / "latest";

    return paths;
  }

  bool ensure_replay_root(const fs::path &baseDir, std::string &err)
  {
    std::error_code ec;
    fs::create_directories(replay_runs_root(baseDir), ec);

    if (ec)
    {
      err = ec.message();
      return false;
    }

    return true;
  }

  bool ensure_replay_run_dir(const ReplayRunPaths &paths, std::string &err)
  {
    std::error_code ec;
    fs::create_directories(paths.run_dir, ec);

    if (ec)
    {
      err = ec.message();
      return false;
    }

    return true;
  }

  bool is_safe_replay_id(const std::string &id)
  {
    if (id.empty())
      return false;

    for (char ch : id)
    {
      const auto c = static_cast<unsigned char>(ch);

      const bool ok =
          std::isalnum(c) != 0 ||
          ch == '-' ||
          ch == '_';

      if (!ok)
        return false;
    }

    return true;
  }

  bool replay_run_exists(const fs::path &baseDir, const std::string &id)
  {
    if (!is_safe_replay_id(id))
      return false;

    std::error_code ec;
    const fs::path dir = replay_run_dir(baseDir, id);

    return fs::exists(dir, ec) && fs::is_directory(dir, ec) && !ec;
  }

} // namespace vix::commands::replay
