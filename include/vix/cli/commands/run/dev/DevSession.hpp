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
#include <map>
#include <string>
#include <vector>

#include <vix/cli/commands/run/RunDetail.hpp>
#include <vix/cli/commands/run/dev/DevChangeClassifier.hpp>
#include <vix/cli/commands/run/dev/DevRebuilder.hpp>

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

  struct DevFileSnapshot
  {
    std::map<std::string, fs::file_time_type> files;

    bool empty() const;
  };

  struct DevDetectedChange
  {
    fs::path path;
    DevChangeKind kind{DevChangeKind::Ignore};

    bool valid() const;
  };

  class DevSession
  {
  public:
    explicit DevSession(DevSessionOptions options);

    const DevSessionOptions &options() const;

    DevSessionResult run();

  private:
    DevSessionOptions options_;
    DevChangeClassifier classifier_;
    DevRebuilder rebuilder_;

    DevFileSnapshot snapshot_project() const;

    std::vector<DevDetectedChange> detect_changes(
        const DevFileSnapshot &before,
        const DevFileSnapshot &after) const;

    DevChangeKind strongest_change_kind(
        const std::vector<DevDetectedChange> &changes) const;

    DevDetectedChange first_relevant_change(
        const std::vector<DevDetectedChange> &changes) const;

    DevDetectedChange wait_for_change(
        DevFileSnapshot &snapshot) const;

    int rebuild_for_change(DevChangeKind kind) const;

    fs::path executable_path() const;

#ifndef _WIN32
    int run_child_once(const fs::path &exePath);
    void stop_child(int pid) const;
#endif

    bool should_skip_directory(const fs::path &path) const;
  };

} // namespace vix::commands::RunCommand::dev

#endif // VIX_CLI_COMMANDS_RUN_DEV_DEV_SESSION_HPP
