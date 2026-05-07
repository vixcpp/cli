/**
 *
 *  @file DevSession.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Dev mode session orchestrator
 *
 */

#include <vix/cli/commands/run/dev/DevSession.hpp>

#include <vix/cli/Style.hpp>
#include <vix/cli/commands/run/RunScriptHelpers.hpp>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <system_error>
#include <thread>
#include <utility>

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace vix::cli::style;

namespace vix::commands::RunCommand::dev
{
  namespace
  {
    std::string path_key(const fs::path &path)
    {
      return path.lexically_normal().generic_string();
    }

    std::string executable_name(const std::string &name)
    {
#ifdef _WIN32
      return name + ".exe";
#else
      return name;
#endif
    }

    bool change_is_stronger(DevChangeKind current, DevChangeKind next)
    {
      if (next == DevChangeKind::ReconfigureAndRebuild)
        return current != DevChangeKind::ReconfigureAndRebuild;

      if (next == DevChangeKind::RebuildOnly)
        return current == DevChangeKind::Ignore;

      return false;
    }
  } // namespace

  bool DevFileSnapshot::empty() const
  {
    return files.empty();
  }

  bool DevDetectedChange::valid() const
  {
    return kind != DevChangeKind::Ignore && !path.empty();
  }

  DevSession::DevSession(DevSessionOptions options)
      : options_(std::move(options)),
        classifier_(),
        rebuilder_(DevRebuilderOptions{
            options_.projectDir,
            options_.buildDir,
            options_.targetName,
            options_.runOptions,
            options_.quiet})
  {
  }

  const DevSessionOptions &DevSession::options() const
  {
    return options_;
  }

  DevSessionResult DevSession::run()
  {
#ifdef _WIN32
    DevSessionResult result;
    result.exitCode = 1;
    result.message = "DevSession is not implemented on Windows.";
    error(result.message);
    return result;
#else
    DevSessionResult result;

    info("Watcher Process started (project hot reload).");
    hint("Watching project: " + options_.projectDir.string());
    hint("Press Ctrl+C to stop dev mode.");

    DevFileSnapshot snapshot = snapshot_project();

    while (true)
    {
      const DevRebuilderResult rebuildResult = rebuilder_.rebuild();

      if (!rebuildResult.ok)
      {
        hint("Fix the errors, save your files, and Vix will rebuild automatically.");

        const DevDetectedChange change = wait_for_change(snapshot);
        if (change.valid())
          detail::print_watch_restart_banner(change.path, "Rebuilding project...");

        continue;
      }

      const fs::path exePath = executable_path();

      if (!fs::exists(exePath))
      {
        result.exitCode = 1;
        result.message = "Dev executable not found: " + exePath.string();

        error(result.message);
        hint("Make sure your CMakeLists.txt defines an executable named '" + options_.targetName + "'.");
        return result;
      }

      const int runCode = run_child_once(exePath);

      if (runCode != 0)
      {
        result.exitCode = runCode;
        result.message = "Dev server exited with code " + std::to_string(runCode) + ".";
        return result;
      }
    }

    result.exitCode = 0;
    result.message = "Dev session stopped.";
    return result;
#endif
  }

  DevFileSnapshot DevSession::snapshot_project() const
  {
    DevFileSnapshot snapshot;

    std::error_code ec;

    if (!fs::exists(options_.projectDir, ec) || ec)
      return snapshot;

    for (auto it = fs::recursive_directory_iterator(
             options_.projectDir,
             fs::directory_options::skip_permission_denied,
             ec);
         !ec && it != fs::recursive_directory_iterator();
         ++it)
    {
      const fs::path path = it->path();

      if (it->is_directory())
      {
        if (should_skip_directory(path))
          it.disable_recursion_pending();

        continue;
      }

      if (!it->is_regular_file())
        continue;

      const DevChangeKind kind = classifier_.classify(options_.projectDir, path);
      if (kind == DevChangeKind::Ignore)
        continue;

      std::error_code timeEc;
      const fs::file_time_type time = fs::last_write_time(path, timeEc);
      if (timeEc)
        continue;

      snapshot.files[path_key(path)] = time;
    }

    return snapshot;
  }

  std::vector<DevDetectedChange> DevSession::detect_changes(
      const DevFileSnapshot &before,
      const DevFileSnapshot &after) const
  {
    std::vector<DevDetectedChange> changes;

    for (const auto &[key, newTime] : after.files)
    {
      const auto oldIt = before.files.find(key);

      if (oldIt == before.files.end() || oldIt->second != newTime)
      {
        const fs::path changedPath = fs::path(key);
        const DevChangeKind kind = classifier_.classify(options_.projectDir, changedPath);

        if (kind != DevChangeKind::Ignore)
          changes.push_back(DevDetectedChange{changedPath, kind});
      }
    }

    for (const auto &[key, oldTime] : before.files)
    {
      (void)oldTime;

      if (after.files.find(key) == after.files.end())
      {
        const fs::path changedPath = fs::path(key);
        const DevChangeKind kind = classifier_.classify(options_.projectDir, changedPath);

        if (kind != DevChangeKind::Ignore)
          changes.push_back(DevDetectedChange{changedPath, kind});
      }
    }

    return changes;
  }

  DevChangeKind DevSession::strongest_change_kind(
      const std::vector<DevDetectedChange> &changes) const
  {
    DevChangeKind strongest = DevChangeKind::Ignore;

    for (const auto &change : changes)
    {
      if (change_is_stronger(strongest, change.kind))
        strongest = change.kind;
    }

    return strongest;
  }

  DevDetectedChange DevSession::first_relevant_change(
      const std::vector<DevDetectedChange> &changes) const
  {
    for (const auto &change : changes)
    {
      if (change.kind == DevChangeKind::ReconfigureAndRebuild)
        return change;
    }

    for (const auto &change : changes)
    {
      if (change.kind == DevChangeKind::RebuildOnly)
        return change;
    }

    return {};
  }

  DevDetectedChange DevSession::wait_for_change(
      DevFileSnapshot &snapshot) const
  {
    while (true)
    {
      std::this_thread::sleep_for(options_.pollInterval);

      DevFileSnapshot next = snapshot_project();
      std::vector<DevDetectedChange> changes = detect_changes(snapshot, next);

      if (changes.empty())
        continue;

      std::this_thread::sleep_for(options_.debounceDelay);

      next = snapshot_project();
      changes = detect_changes(snapshot, next);

      snapshot = std::move(next);

      const DevDetectedChange change = first_relevant_change(changes);
      if (change.valid())
        return change;
    }
  }

  int DevSession::rebuild_for_change(DevChangeKind kind) const
  {
    if (kind == DevChangeKind::ReconfigureAndRebuild)
    {
      const DevRebuilderResult result = rebuilder_.reconfigure_and_rebuild();
      return result.ok ? 0 : result.exitCode;
    }

    if (kind == DevChangeKind::RebuildOnly)
    {
      const DevRebuilderResult result = rebuilder_.rebuild();
      return result.ok ? 0 : result.exitCode;
    }

    return 0;
  }

  fs::path DevSession::executable_path() const
  {
    return options_.buildDir / executable_name(options_.targetName);
  }

#ifndef _WIN32
  int DevSession::run_child_once(const fs::path &exePath)
  {
    using Clock = std::chrono::steady_clock;

    const auto childStart = Clock::now();

    pid_t pid = ::fork();
    if (pid < 0)
    {
      error("Failed to fork() for dev process.");
      return 1;
    }

    if (pid == 0)
    {
      const std::string runCwd = options_.runOptions.cwd.empty()
                                     ? options_.projectDir.string()
                                     : detail::normalize_cwd_if_needed(options_.runOptions.cwd);

      if (::chdir(runCwd.c_str()) != 0)
      {
        std::cerr << "[vix][run] chdir failed: " << std::strerror(errno) << "\n";
        _exit(127);
      }

      if (::setenv("VIX_STDOUT_MODE", "line", 1) != 0)
      {
        std::cerr << "[vix][run] setenv VIX_STDOUT_MODE failed: " << std::strerror(errno) << "\n";
        _exit(127);
      }

      if (::setenv("VIX_MODE", "dev", 1) != 0)
      {
        std::cerr << "[vix][run] setenv VIX_MODE failed: " << std::strerror(errno) << "\n";
        _exit(127);
      }

      if (options_.runOptions.withMySql)
      {
        ::setenv("VIX_DB_ENGINE", "mysql", 1);
        ::setenv("VIX_ENABLE_DB", "1", 1);
        ::setenv("VIX_DB_USE_MYSQL", "1", 1);
      }

      if (options_.runOptions.withSqlite)
      {
        ::setenv("VIX_DB_ENGINE", "sqlite", 1);
        ::setenv("VIX_ENABLE_DB", "1", 1);
        ::setenv("VIX_DB_USE_SQLITE", "1", 1);
      }

      detail::apply_sanitizer_env_if_needed(
          options_.runOptions.enableSanitizers,
          options_.runOptions.enableUbsanOnly,
          options_.runOptions.enableThreadSanitizer);

      std::vector<std::string> argvStr;
      argvStr.push_back(exePath.string());

      for (const auto &arg : options_.runOptions.runArgs)
      {
        if (!arg.empty())
          argvStr.push_back(arg);
      }

      std::vector<char *> argv;
      argv.reserve(argvStr.size() + 1);

      for (auto &arg : argvStr)
        argv.push_back(const_cast<char *>(arg.c_str()));

      argv.push_back(nullptr);

      ::execv(argv[0], argv.data());

      std::cerr << "[vix][run] execv failed: " << std::strerror(errno) << "\n";
      _exit(127);
    }

    bool needRestart = false;
    bool running = true;

    while (running)
    {
      std::this_thread::sleep_for(options_.pollInterval);

      DevFileSnapshot before = snapshot_project();
      std::this_thread::sleep_for(options_.debounceDelay);
      DevFileSnapshot after = snapshot_project();

      std::vector<DevDetectedChange> changes = detect_changes(before, after);

      if (!changes.empty())
      {
        const DevDetectedChange change = first_relevant_change(changes);
        const DevChangeKind kind = strongest_change_kind(changes);

        if (change.valid())
        {
          const std::string label =
              kind == DevChangeKind::ReconfigureAndRebuild
                  ? "Reconfiguring project..."
                  : "Rebuilding project...";

          detail::print_watch_restart_banner(change.path, label);
        }

        needRestart = true;
        stop_child(pid);

        int status = 0;
        (void)::waitpid(pid, &status, 0);

        const int rebuildCode = rebuild_for_change(kind);

        if (rebuildCode != 0)
        {
          hint("Fix the errors, save your files, and Vix will rebuild automatically.");
          DevFileSnapshot stableSnapshot = after;
          (void)wait_for_change(stableSnapshot);
        }

        return 0;
      }

      int status = 0;
      const pid_t r = ::waitpid(pid, &status, WNOHANG);

      if (r == pid)
      {
        running = false;

        const auto childEnd = Clock::now();
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                childEnd - childStart)
                .count();

        int exitCode = 0;

        if (WIFEXITED(status))
          exitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
          exitCode = 128 + WTERMSIG(status);

        if (!needRestart)
        {
          if (exitCode != 0)
          {
            error("Dev server exited with code " +
                  std::to_string(exitCode) +
                  " (lifetime ~" + std::to_string(ms) + "ms).");
          }
          else
          {
            success("Dev server stopped cleanly (lifetime ~" +
                    std::to_string(ms) + "ms).");
          }

          return exitCode;
        }
      }
    }

    return 0;
  }

  void DevSession::stop_child(int pid) const
  {
    if (pid <= 0)
      return;

    if (::kill(pid, SIGINT) != 0)
    {
    }
  }
#endif

  bool DevSession::should_skip_directory(const fs::path &path) const
  {
    const std::string name = path.filename().string();

    return name == ".git" ||
           name == ".vix" ||
           name == "build" ||
           name == "build-dev" ||
           name == "build-ninja" ||
           name == "build-release" ||
           name == "node_modules" ||
           name == ".cache" ||
           name == ".idea" ||
           name == ".vscode";
  }

} // namespace vix::commands::RunCommand::dev
