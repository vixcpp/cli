/**
 *
 *  @file DevSession.hpp
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

#ifndef VIX_CLI_COMMANDS_RUN_DEV_DEV_SESSION_HPP
#define VIX_CLI_COMMANDS_RUN_DEV_DEV_SESSION_HPP

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>

#include <vix/cli/commands/run/RunDetail.hpp>
#include <vix/cli/commands/run/dev/DevRebuilder.hpp>
#include <vix/cli/commands/run/dev/DevFileIndex.hpp>

#include <vix/async/core/io_context.hpp>
#include <vix/async/core/task.hpp>
#include <vix/async/core/cancel.hpp>

namespace vix::commands::RunCommand::dev
{
  namespace fs = std::filesystem;
  namespace detail = vix::commands::RunCommand::detail;

  struct DevSessionOptions
  {
    fs::path projectDir;
    fs::path buildDir;
    std::string targetName;

    detail::Options runOptions;

    std::chrono::milliseconds pollInterval{300};
    std::chrono::milliseconds debounceDelay{200};

    bool quiet{false};
  };

  struct DevSessionResult
  {
    int exitCode{0};
    std::string message;
  };

  enum class DevChildExitReason
  {
    Exited,
    RestartRequested,
    Cancelled
  };

  struct DevChildRunResult
  {
    int exitCode{0};
    DevChildExitReason reason{DevChildExitReason::Exited};
  };

  class DevSession
  {
  public:
    explicit DevSession(DevSessionOptions options);

    const DevSessionOptions &options() const;

    DevSessionResult run();

  private:
#ifndef _WIN32
    vix::async::core::task<void> watch_stop_signals_async(
        vix::async::core::io_context &ctx,
        vix::async::core::cancel_source &cancel);
#endif

    vix::async::core::task<DevSessionResult> run_async(
        vix::async::core::io_context &ctx,
        vix::async::core::cancel_token ct);

    vix::async::core::task<void> sleep_poll_interval(
        vix::async::core::io_context &ctx,
        vix::async::core::cancel_token ct) const;

    vix::async::core::task<void> sleep_debounce_delay(
        vix::async::core::io_context &ctx,
        vix::async::core::cancel_token ct) const;

    vix::async::core::task<DevRebuilderResult> rebuild_async(
        vix::async::core::io_context &ctx,
        DevChangeKind kind,
        vix::async::core::cancel_token ct) const;

    vix::async::core::task<DevIndexedChange> wait_for_indexed_change_async(
        vix::async::core::io_context &ctx,
        vix::async::core::cancel_token ct);

    DevIndexedChange select_relevant_indexed_change(
        const std::vector<DevIndexedChange> &changes) const;

    void print_reload_for_change(const DevIndexedChange &change) const;

    DevSessionOptions options_;
    DevRebuilder rebuilder_;
    DevFileIndex fileIndex_;
    DevChangeKind pendingChangeKind_{DevChangeKind::Ignore};

#ifndef _WIN32
    int vueFrontendPid_{-1};
#endif

    std::optional<fs::path> executable_path() const;

#ifndef _WIN32
    vix::async::core::task<DevChildRunResult> run_child_once_async(
        vix::async::core::io_context &ctx,
        const fs::path &exePath,
        vix::async::core::cancel_token ct);

    [[noreturn]] void exec_child_process(const fs::path &exePath) const;

    vix::async::core::task<void> terminate_and_wait_child_async(
        vix::async::core::io_context &ctx,
        int pid,
        vix::async::core::cancel_token ct) const;

    bool has_vue_frontend() const;
    int start_vue_frontend();
    void stop_vue_frontend();
#endif
  };

} // namespace vix::commands::RunCommand::dev

#endif // VIX_CLI_COMMANDS_RUN_DEV_DEV_SESSION_HPP
