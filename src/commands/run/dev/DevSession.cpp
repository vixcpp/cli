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
#include <vix/cli/errors/RawLogDetectors.hpp>
#include <vix/async/core/io_context.hpp>
#include <vix/async/core/timer.hpp>
#include <vix/async/core/thread_pool.hpp>
#include <vix/async/core/cancel.hpp>
#include <vix/async/core/signal.hpp>
#include <vix/utils/Env.hpp>
#include <cctype>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <system_error>
#include <utility>
#include <optional>
#include <algorithm>
#include <fstream>

#ifndef _WIN32
#include <fcntl.h>
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
    std::string executable_name(const std::string &name)
    {
#ifdef _WIN32
      return name + ".exe";
#else
      return name;
#endif
    }

    void clear_dev_screen()
    {
      std::cout << "\033[2J\033[H" << std::flush;
    }

    bool dev_verbose_ui(const DevSessionOptions &options)
    {
      if (options.runOptions.verbose)
        return true;

      const char *lvl = vix::utils::vix_getenv("VIX_LOG_LEVEL");
      if (!lvl || !*lvl)
        return false;

      std::string s(lvl);
      for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      return s == "debug" || s == "trace";
    }

    void print_dev_header(const DevSessionOptions &options)
    {
      if (options.quiet)
        return;

      std::cout << CYAN << BOLD << "Dev " << RESET
                << CYAN << BOLD << options.targetName << RESET
                << GRAY << " (dev)" << RESET
                << "\n";

      if (!dev_verbose_ui(options))
        return;

      std::cout << "  "
                << GRAY << "watching: " << RESET
                << options.projectDir.string()
                << "\n";

      std::cout << "  "
                << GRAY << "target  : " << RESET
                << options.targetName
                << "\n";

      std::cout << "  "
                << GRAY << "build   : " << RESET
                << options.buildDir.string()
                << "\n";

      std::cout << "  "
                << GRAY << "press Ctrl+C to stop" << RESET
                << "\n\n";
    }

    void print_dev_started(int pid)
    {
      std::cout << "  "
                << GREEN << "✔" << RESET
                << " Started"
                << GRAY << " pid=" << pid << RESET
                << "\n";
    }

    void print_dev_app_exited_cleanly()
    {
      std::cout << "  "
                << CYAN << "•" << RESET
                << " Dev app exited cleanly"
                << GRAY << ". Session stopped." << RESET
                << "\n";
    }

#ifndef _WIN32
    bool wait_child_nonblocking(pid_t pid, int &status)
    {
      while (true)
      {
        const pid_t r = ::waitpid(pid, &status, WNOHANG);

        if (r == pid)
          return true;

        if (r == 0)
          return false;

        if (r < 0 && errno == EINTR)
          continue;

        return true;
      }
    }
#endif

#ifndef _WIN32
    bool set_nonblocking_fd(int fd)
    {
      const int flags = ::fcntl(fd, F_GETFL, 0);

      if (flags < 0)
        return false;

      return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    void drain_fd_live(int fd, std::string &out)
    {
      char buffer[4096];

      while (true)
      {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));

        if (n > 0)
        {
          out.append(buffer, static_cast<std::size_t>(n));
          std::cout.write(buffer, n);
          std::cout.flush();
          continue;
        }

        if (n == 0)
          return;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
          return;

        if (errno == EINTR)
          continue;

        return;
      }
    }
#endif
  } // namespace

  DevSession::DevSession(DevSessionOptions options)
      : options_(std::move(options)),
        rebuilder_(DevRebuilderOptions{
            options_.projectDir,
            options_.buildDir,
            options_.targetName,
            options_.runOptions,
            options_.quiet}),
        fileIndex_(options_.projectDir)
  {
  }

  const DevSessionOptions &DevSession::options() const
  {
    return options_;
  }

  vix::async::core::task<void> DevSession::sleep_poll_interval(
      vix::async::core::io_context &ctx,
      vix::async::core::cancel_token ct) const
  {
    co_await ctx.timers().sleep_for(options_.pollInterval, std::move(ct));
  }

  vix::async::core::task<void> DevSession::sleep_debounce_delay(
      vix::async::core::io_context &ctx,
      vix::async::core::cancel_token ct) const
  {
    co_await ctx.timers().sleep_for(options_.debounceDelay, std::move(ct));
  }

  vix::async::core::task<DevRebuilderResult> DevSession::rebuild_async(
      vix::async::core::io_context &ctx,
      DevChangeKind kind,
      vix::async::core::cancel_token ct) const
  {
    if (ct.is_cancelled())
    {
      DevRebuilderResult result;
      result.ok = false;
      result.exitCode = 130;
      result.message = "Dev rebuild cancelled.";
      co_return result;
    }

    co_return co_await ctx.cpu_pool().submit(
        [this, kind]()
        {
          if (kind == DevChangeKind::ReconfigureAndRebuild)
          {
            return rebuilder_.reconfigure_and_rebuild();
          }

          return rebuilder_.rebuild();
        },
        std::move(ct));
  }

  DevIndexedChange DevSession::select_relevant_indexed_change(
      const std::vector<DevIndexedChange> &changes) const
  {
    DevIndexedChange selected{};

    for (const auto &change : changes)
    {
      if (!change.valid())
        continue;

      if (change.kind == DevChangeKind::ReconfigureAndRebuild)
        return change;

      if (!selected.valid())
        selected = change;
    }

    return selected;
  }

  void DevSession::print_reload_for_change(
      const DevIndexedChange &change) const
  {
    if (options_.quiet || !change.valid())
      return;

    clear_dev_screen();

    std::cout << CYAN << BOLD << "Dev " << RESET
              << CYAN << BOLD << options_.targetName << RESET
              << GRAY << " (dev)" << RESET
              << "\n";

    std::cout << "  "
              << GRAY << "changed: " << RESET
              << change.path.string()
              << "\n";
  }

  vix::async::core::task<DevIndexedChange> DevSession::wait_for_indexed_change_async(
      vix::async::core::io_context &ctx,
      vix::async::core::cancel_token ct)
  {
    while (!ct.is_cancelled())
    {
      std::vector<DevIndexedChange> changes = fileIndex_.poll_changes();

      if (!changes.empty())
      {
        DevIndexedChange selected =
            select_relevant_indexed_change(changes);

        if (selected.valid())
        {
          co_await sleep_debounce_delay(ctx, ct);

          std::vector<DevIndexedChange> debouncedChanges = fileIndex_.poll_changes();

          if (!debouncedChanges.empty())
          {
            DevIndexedChange debouncedSelected =
                select_relevant_indexed_change(debouncedChanges);

            if (debouncedSelected.valid())
              selected = debouncedSelected;
          }

          co_return selected;
        }
      }

      co_await sleep_poll_interval(ctx, ct);
    }

    co_return DevIndexedChange{};
  }

  vix::async::core::task<DevSessionResult> DevSession::run_async(
      vix::async::core::io_context &ctx,
      vix::async::core::cancel_token ct)
  {
#ifdef _WIN32
    (void)ctx;
    (void)ct;

    DevSessionResult result;
    result.exitCode = 1;
    result.message = "DevSession is not implemented on Windows.";
    error(result.message);
    co_return result;
#else
    DevSessionResult result;

    struct FrontendGuard
    {
      DevSession *session{nullptr};

      ~FrontendGuard()
      {
        if (session)
          session->stop_vue_frontend();
      }
    };

    FrontendGuard frontendGuard{this};

    print_dev_header(options_);

    const bool vueFrontend = has_vue_frontend();

    if (vueFrontend)
    {
      const int frontendCode = start_vue_frontend();
      if (frontendCode != 0)
      {
        result.exitCode = frontendCode;
        result.message = "Failed to start Vue frontend.";
        co_return result;
      }
    }

    bool fileIndexReady = false;

    while (!ct.is_cancelled())
    {
      const DevChangeKind rebuildKind = pendingChangeKind_;
      pendingChangeKind_ = DevChangeKind::Ignore;

      DevRebuilderResult rebuildResult =
          co_await rebuild_async(ctx, rebuildKind, ct);

      if (ct.is_cancelled())
      {
        result.exitCode = 130;
        result.message = "Dev session cancelled.";
        co_return result;
      }

      if (!rebuildResult.ok)
      {
        hint("Fix the errors, save your files, and Vix will rebuild automatically.");

        fileIndex_.refresh();
        fileIndexReady = true;

        DevIndexedChange change =
            co_await wait_for_indexed_change_async(ctx, ct);

        if (!change.valid())
        {
          result.exitCode = 130;
          result.message = "Dev session cancelled.";
          co_return result;
        }

        pendingChangeKind_ = change.kind;
        continue;
      }

      const std::optional<fs::path> exePath = executable_path();

      if (!exePath)
      {
        if (!fileIndexReady)
        {
          fileIndex_.refresh();
          fileIndexReady = true;
        }

        if (!options_.quiet)
        {
          success("Library built.");
          hint("No runnable executable found. Watching for changes.");
          hint("Use `vix tests` to run the test suite.");
        }

        DevIndexedChange change =
            co_await wait_for_indexed_change_async(ctx, ct);

        if (!change.valid())
        {
          result.exitCode = 130;
          result.message = "Dev session cancelled.";
          co_return result;
        }

        pendingChangeKind_ = change.kind;
        continue;
      }

      if (!fileIndexReady)
      {
        fileIndex_.refresh();
        fileIndexReady = true;
      }

      const DevChildRunResult childResult =
          co_await run_child_once_async(ctx, *exePath, ct);

      if (childResult.reason == DevChildExitReason::RestartRequested)
        continue;

      if (childResult.reason == DevChildExitReason::Cancelled)
      {
        result.exitCode = 0;
        result.message = "Dev session stopped.";
        co_return result;
      }

      result.exitCode = childResult.exitCode;

      if (childResult.exitCode == 0)
      {
        if (!options_.quiet)
          print_dev_app_exited_cleanly();

        result.message = "Dev app exited cleanly.";
      }
      else
      {
        result.message =
            "Dev server exited with code " + std::to_string(childResult.exitCode) + ".";
      }

      co_return result;
    }

    result.exitCode = 0;
    result.message = "Dev session stopped.";
    co_return result;
#endif
  }

#ifndef _WIN32
  vix::async::core::task<void> DevSession::watch_stop_signals_async(
      vix::async::core::io_context &ctx,
      vix::async::core::cancel_source &cancel)
  {
    auto &signals = ctx.signals();

    signals.add(SIGINT);
    signals.add(SIGTERM);

    try
    {
      const int sig = co_await signals.async_wait(cancel.token());

      if (!options_.quiet)
      {
        std::cout << "\n";
        hint("Stopping dev session...");
      }

      (void)sig;

      cancel.request_cancel();
      ctx.stop();
    }
    catch (...)
    {
      cancel.request_cancel();
      ctx.stop();
    }

    co_return;
  }
#endif

  DevSessionResult DevSession::run()
  {
#ifdef _WIN32
    DevSessionResult result;
    result.exitCode = 1;
    result.message = "DevSession is not implemented on Windows.";
    error(result.message);
    return result;
#else
    vix::async::core::io_context ctx;
    vix::async::core::cancel_source cancel;
    auto signalTask = watch_stop_signals_async(ctx, cancel);
    std::move(signalTask).start(ctx.get_scheduler());

    DevSessionResult result;
    bool completed = false;

    auto mainTask =
        [this, &ctx, &cancel, &result, &completed]() -> vix::async::core::task<void>
    {
      try
      {
        result = co_await run_async(ctx, cancel.token());
      }
      catch (const std::exception &e)
      {
        result.exitCode = 1;
        result.message = e.what();

        if (!result.message.empty())
          error(result.message);
      }
      catch (...)
      {
        result.exitCode = 1;
        result.message = "Dev session failed with an unknown error.";
        error(result.message);
      }

      completed = true;
      ctx.stop();

      co_return;
    }();

    std::move(mainTask).start(ctx.get_scheduler());

    ctx.run();
    ctx.shutdown();

    if (!completed)
    {
      result.exitCode = cancel.is_cancelled() ? 0 : 1;
      result.message = cancel.is_cancelled()
                           ? "Dev session stopped."
                           : "Dev session stopped before completion.";
    }

    return result;
#endif
  }

  std::optional<fs::path> DevSession::executable_path() const
  {
    const std::string exeName = executable_name(options_.targetName);

    auto is_executable_candidate = [](const fs::path &path) -> bool
    {
      std::error_code ec;

      if (!fs::is_regular_file(path, ec) || ec)
        return false;

#ifdef _WIN32
      return path.extension() == ".exe";
#else
      const auto perms = fs::status(path, ec).permissions();
      if (ec)
        return false;

      using pr = fs::perms;

      return (perms & pr::owner_exec) != pr::none ||
             (perms & pr::group_exec) != pr::none ||
             (perms & pr::others_exec) != pr::none;
#endif
    };

    auto looks_like_test_binary = [](const fs::path &path) -> bool
    {
      const std::string name = path.filename().string();

      return name.find("_test") != std::string::npos ||
             name.find("_tests") != std::string::npos ||
             name.rfind("test_", 0) == 0;
    };

    const std::vector<fs::path> preferred = {
        options_.buildDir / exeName,
        options_.buildDir / "bin" / exeName,
        options_.buildDir / "src" / exeName};

    for (const auto &path : preferred)
    {
      if (is_executable_candidate(path))
        return path;
    }

    std::vector<fs::path> exactNameCandidates;
    std::vector<fs::path> otherCandidates;

    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             options_.buildDir,
             fs::directory_options::skip_permission_denied,
             ec);
         !ec && it != fs::recursive_directory_iterator();
         ++it)
    {
      const fs::path path = it->path();

      if (path.string().find("CMakeFiles") != std::string::npos)
        continue;

      if (!is_executable_candidate(path))
        continue;

      if (looks_like_test_binary(path))
        continue;

#ifdef _WIN32
      const std::string baseName = path.stem().string();
#else
      const std::string baseName = path.filename().string();
#endif

      if (baseName == options_.targetName)
        exactNameCandidates.push_back(path);
      else
        otherCandidates.push_back(path);
    }

    auto prefer_bin_path = [](const fs::path &a, const fs::path &b) -> bool
    {
      const bool aBin = a.string().find("/bin/") != std::string::npos ||
                        a.string().find("\\bin\\") != std::string::npos;

      const bool bBin = b.string().find("/bin/") != std::string::npos ||
                        b.string().find("\\bin\\") != std::string::npos;

      if (aBin != bBin)
        return aBin;

      return a.string().size() < b.string().size();
    };

    if (!exactNameCandidates.empty())
    {
      std::sort(exactNameCandidates.begin(), exactNameCandidates.end(), prefer_bin_path);
      return exactNameCandidates.front();
    }

    if (otherCandidates.size() == 1)
      return otherCandidates.front();

    return std::nullopt;
  }

#ifndef _WIN32
  bool DevSession::has_vue_frontend() const
  {
    const fs::path manifest = options_.projectDir / "vix.json";

    std::error_code ec;
    if (!fs::exists(manifest, ec) || ec)
      return false;

    std::ifstream in(manifest);
    if (!in)
      return false;

    std::string content(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());

    return content.find("\"template\"") != std::string::npos &&
           content.find("\"vue\"") != std::string::npos &&
           content.find("\"frontend\"") != std::string::npos &&
           fs::exists(options_.projectDir / "frontend" / "package.json", ec) &&
           !ec;
  }
#endif

#ifndef _WIN32
  int DevSession::start_vue_frontend()
  {
    if (!has_vue_frontend())
      return 0;

    if (vueFrontendPid_ > 0)
      return 0;

    const fs::path frontendDir = options_.projectDir / "frontend";

    pid_t pid = ::fork();

    if (pid < 0)
    {
      error("Failed to fork() for Vue dev server.");
      return 1;
    }

    if (pid == 0)
    {
      if (::chdir(frontendDir.string().c_str()) != 0)
      {
        std::cerr << "[vix][vue] chdir failed: " << std::strerror(errno) << "\n";
        _exit(127);
      }

      ::setenv("VIX_FRONTEND", "vue", 1);

      execlp("npm", "npm", "run", "dev", static_cast<char *>(nullptr));

      std::cerr << "[vix][vue] failed to start npm run dev: "
                << std::strerror(errno) << "\n";

      _exit(127);
    }

    vueFrontendPid_ = static_cast<int>(pid);

    if (!options_.quiet)
    {
      std::cout << "  "
                << GREEN << "✔" << RESET
                << " Vue dev server"
                << GRAY << " pid=" << vueFrontendPid_ << RESET
                << "\n";
    }

    return 0;
  }
#endif

#ifndef _WIN32
  void DevSession::stop_vue_frontend()
  {
    if (vueFrontendPid_ <= 0)
      return;

    const pid_t pid = static_cast<pid_t>(vueFrontendPid_);

    if (::kill(pid, SIGINT) != 0)
    {
      vueFrontendPid_ = -1;
      return;
    }

    int status = 0;
    (void)::waitpid(pid, &status, 0);

    vueFrontendPid_ = -1;
  }
#endif

#ifndef _WIN32
  [[noreturn]] void DevSession::exec_child_process(const fs::path &exePath) const
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
#endif

#ifndef _WIN32
  vix::async::core::task<DevChildRunResult> DevSession::run_child_once_async(
      vix::async::core::io_context &ctx,
      const fs::path &exePath,
      vix::async::core::cancel_token ct)
  {
    using Clock = std::chrono::steady_clock;

    const auto childStart = Clock::now();

    int outputPipe[2] = {-1, -1};

    if (::pipe(outputPipe) != 0)
    {
      error("Failed to create dev output pipe.");

      co_return DevChildRunResult{
          1,
          DevChildExitReason::Exited};
    }

    pid_t pid = ::fork();
    if (pid < 0)
    {
      ::close(outputPipe[0]);
      ::close(outputPipe[1]);

      error("Failed to fork() for dev process.");

      co_return DevChildRunResult{
          1,
          DevChildExitReason::Exited};
    }

    if (pid == 0)
    {
      ::close(outputPipe[0]);

      ::dup2(outputPipe[1], STDOUT_FILENO);
      ::dup2(outputPipe[1], STDERR_FILENO);

      ::close(outputPipe[1]);

      exec_child_process(exePath);
    }

    ::close(outputPipe[1]);
    set_nonblocking_fd(outputPipe[0]);

    std::string runtimeLog;

    if (!options_.quiet)
      print_dev_started(static_cast<int>(pid));

    while (!ct.is_cancelled())
    {
      co_await sleep_poll_interval(ctx, ct);
      drain_fd_live(outputPipe[0], runtimeLog);

      std::vector<DevIndexedChange> indexedChanges = fileIndex_.poll_changes();

      if (!indexedChanges.empty())
      {
        co_await sleep_debounce_delay(ctx, ct);

        std::vector<DevIndexedChange> debouncedChanges = fileIndex_.poll_changes();

        if (debouncedChanges.empty())
          debouncedChanges = std::move(indexedChanges);

        DevIndexedChange selected =
            select_relevant_indexed_change(debouncedChanges);

        if (!selected.valid())
          continue;

        print_reload_for_change(selected);
        pendingChangeKind_ = selected.kind;

        co_await terminate_and_wait_child_async(
            ctx,
            static_cast<int>(pid),
            ct);

        drain_fd_live(outputPipe[0], runtimeLog);
        ::close(outputPipe[0]);

        co_return DevChildRunResult{
            0,
            DevChildExitReason::RestartRequested};
      }

      int status = 0;
      const pid_t r = ::waitpid(pid, &status, WNOHANG);

      if (r == pid)
      {
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

        drain_fd_live(outputPipe[0], runtimeLog);
        ::close(outputPipe[0]);

        if (exitCode != 0)
        {
          bool handled = false;

          if (!runtimeLog.empty())
          {
            handled = vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
                runtimeLog,
                exePath,
                "Dev server exited with code " + std::to_string(exitCode));
          }

          if (!handled)
          {
            error("Dev server exited with code " +
                  std::to_string(exitCode) +
                  " (lifetime ~" + std::to_string(ms) + "ms).");
          }
        }
        else
        {
          success("Dev server stopped cleanly (lifetime ~" +
                  std::to_string(ms) + "ms).");
        }

        co_return DevChildRunResult{
            exitCode,
            DevChildExitReason::Exited};
      }
    }

    co_await terminate_and_wait_child_async(
        ctx,
        static_cast<int>(pid),
        ct);

    co_return DevChildRunResult{
        130,
        DevChildExitReason::Cancelled};
  }

  vix::async::core::task<void> DevSession::terminate_and_wait_child_async(
      vix::async::core::io_context &ctx,
      int pid,
      vix::async::core::cancel_token ct) const
  {
    if (pid <= 0)
      co_return;

    const pid_t child = static_cast<pid_t>(pid);
    int status = 0;

    auto wait_for_exit =
        [&](std::chrono::milliseconds timeout) -> vix::async::core::task<bool>
    {
      const auto deadline = std::chrono::steady_clock::now() + timeout;

      while (std::chrono::steady_clock::now() < deadline)
      {
        if (wait_child_nonblocking(child, status))
          co_return true;

        if (ct.is_cancelled())
          co_return false;

        co_await ctx.timers().sleep_for(
            std::chrono::milliseconds(20),
            ct);
      }

      co_return wait_child_nonblocking(child, status);
    };

    if (::kill(child, SIGINT) == 0)
    {
      if (co_await wait_for_exit(std::chrono::milliseconds(800)))
        co_return;
    }

    if (::kill(child, SIGTERM) == 0)
    {
      if (co_await wait_for_exit(std::chrono::milliseconds(800)))
        co_return;
    }

    if (::kill(child, SIGKILL) == 0)
    {
      while (true)
      {
        const pid_t r = ::waitpid(child, &status, 0);

        if (r == child)
          co_return;

        if (r < 0 && errno == EINTR)
          continue;

        co_return;
      }
    }

    co_return;
  }
#endif

} // namespace vix::commands::RunCommand::dev
