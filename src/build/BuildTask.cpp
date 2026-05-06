/**
 *
 *  @file BuildTask.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Build graph task model
 *
 */

#include <vix/cli/build/BuildTask.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <sstream>

namespace vix::cli::build
{
  namespace
  {
    static constexpr std::uint64_t FNV_OFFSET = 1469598103934665603ull;
    static constexpr std::uint64_t FNV_PRIME = 1099511628211ull;

    static std::uint64_t fnv_mix(
        std::uint64_t h,
        const void *data,
        std::size_t len)
    {
      const auto *p = static_cast<const unsigned char *>(data);

      for (std::size_t i = 0; i < len; ++i)
      {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= FNV_PRIME;
      }

      return h;
    }

    static std::uint64_t fnv_mix_string(
        std::uint64_t h,
        const std::string &value)
    {
      return fnv_mix(h, value.data(), value.size());
    }

    static std::string hex64(std::uint64_t value)
    {
      static constexpr char digits[] = "0123456789abcdef";

      std::string out(16, '0');

      for (int i = 15; i >= 0; --i)
      {
        out[static_cast<std::size_t>(i)] = digits[value & 0x0f];
        value >>= 4;
      }

      return out;
    }

    static bool contains_string(
        const std::vector<std::string> &items,
        const std::string &value)
    {
      return std::find(items.begin(), items.end(), value) != items.end();
    }

    static std::uint64_t now_unix_ms()
    {
      using namespace std::chrono;

      return static_cast<std::uint64_t>(
          duration_cast<milliseconds>(
              system_clock::now().time_since_epoch())
              .count());
    }
  } // namespace

  std::string to_string(BuildTaskKind kind)
  {
    switch (kind)
    {
    case BuildTaskKind::Compile:
      return "compile";
    case BuildTaskKind::Link:
      return "link";
    case BuildTaskKind::Archive:
      return "archive";
    case BuildTaskKind::Copy:
      return "copy";
    case BuildTaskKind::Generate:
      return "generate";
    case BuildTaskKind::Unknown:
    default:
      return "unknown";
    }
  }

  std::string to_string(BuildTaskState state)
  {
    switch (state)
    {
    case BuildTaskState::Pending:
      return "pending";
    case BuildTaskState::Ready:
      return "ready";
    case BuildTaskState::Running:
      return "running";
    case BuildTaskState::Done:
      return "done";
    case BuildTaskState::Failed:
      return "failed";
    case BuildTaskState::Skipped:
      return "skipped";
    default:
      return "pending";
    }
  }

  BuildTaskKind build_task_kind_from_string(const std::string &value)
  {
    if (value == "compile")
      return BuildTaskKind::Compile;
    if (value == "link")
      return BuildTaskKind::Link;
    if (value == "archive")
      return BuildTaskKind::Archive;
    if (value == "copy")
      return BuildTaskKind::Copy;
    if (value == "generate")
      return BuildTaskKind::Generate;

    return BuildTaskKind::Unknown;
  }

  BuildTaskState build_task_state_from_string(const std::string &value)
  {
    if (value == "ready")
      return BuildTaskState::Ready;
    if (value == "running")
      return BuildTaskState::Running;
    if (value == "done")
      return BuildTaskState::Done;
    if (value == "failed")
      return BuildTaskState::Failed;
    if (value == "skipped")
      return BuildTaskState::Skipped;
    if (value == "pending")
      return BuildTaskState::Pending;

    return BuildTaskState::Pending;
  }

  bool BuildTask::valid() const
  {
    return !id.empty();
  }

  bool BuildTask::pending() const
  {
    return state == BuildTaskState::Pending;
  }

  bool BuildTask::ready() const
  {
    return state == BuildTaskState::Ready;
  }

  bool BuildTask::running() const
  {
    return state == BuildTaskState::Running;
  }

  bool BuildTask::done() const
  {
    return state == BuildTaskState::Done;
  }

  bool BuildTask::failed() const
  {
    return state == BuildTaskState::Failed;
  }

  bool BuildTask::skipped() const
  {
    return state == BuildTaskState::Skipped;
  }

  void BuildTask::mark_pending()
  {
    state = BuildTaskState::Pending;
  }

  void BuildTask::mark_ready()
  {
    state = BuildTaskState::Ready;
  }

  void BuildTask::mark_running()
  {
    state = BuildTaskState::Running;
    startedUnixMs = now_unix_ms();
    finishedUnixMs = 0;
    exitCode = 0;
  }

  void BuildTask::mark_done()
  {
    state = BuildTaskState::Done;
    finishedUnixMs = now_unix_ms();
    exitCode = 0;
  }

  void BuildTask::mark_failed(int code)
  {
    state = BuildTaskState::Failed;
    finishedUnixMs = now_unix_ms();
    exitCode = code;
  }

  void BuildTask::mark_skipped()
  {
    state = BuildTaskState::Skipped;
    finishedUnixMs = now_unix_ms();
    exitCode = 0;
  }

  void BuildTask::add_input(const std::string &nodeId)
  {
    if (nodeId.empty())
      return;

    if (has_input(nodeId))
      return;

    inputs.push_back(nodeId);
  }

  void BuildTask::add_output(const std::string &nodeId)
  {
    if (nodeId.empty())
      return;

    if (has_output(nodeId))
      return;

    outputs.push_back(nodeId);
  }

  void BuildTask::add_dependency(const std::string &taskId)
  {
    if (taskId.empty())
      return;

    if (has_dependency(taskId))
      return;

    deps.push_back(taskId);
  }

  bool BuildTask::has_input(const std::string &nodeId) const
  {
    return contains_string(inputs, nodeId);
  }

  bool BuildTask::has_output(const std::string &nodeId) const
  {
    return contains_string(outputs, nodeId);
  }

  bool BuildTask::has_dependency(const std::string &taskId) const
  {
    return contains_string(deps, taskId);
  }

  std::string make_build_task_id(
      BuildTaskKind kind,
      const std::string &outputId)
  {
    std::ostringstream oss;
    oss << to_string(kind) << ":";
    oss << outputId;
    return oss.str();
  }

  std::string hash_build_command(const std::vector<std::string> &command)
  {
    std::uint64_t h = FNV_OFFSET;

    for (const auto &arg : command)
    {
      h = fnv_mix_string(h, arg);

      const char separator = '\0';
      h = fnv_mix(h, &separator, sizeof(separator));
    }

    return hex64(h);
  }

  BuildTask make_compile_task(
      const std::string &sourceNodeId,
      const std::string &objectNodeId,
      const std::vector<std::string> &command,
      const fs::path &workingDirectory)
  {
    BuildTask task;
    task.kind = BuildTaskKind::Compile;
    task.state = BuildTaskState::Pending;
    task.id = make_build_task_id(task.kind, objectNodeId);
    task.inputs.push_back(sourceNodeId);
    task.outputs.push_back(objectNodeId);
    task.command = command;
    task.commandHash = hash_build_command(command);
    task.workingDirectory = workingDirectory.lexically_normal();

    return task;
  }

  BuildTask make_link_task(
      const std::vector<std::string> &inputNodeIds,
      const std::string &executableNodeId,
      const std::vector<std::string> &command,
      const fs::path &workingDirectory)
  {
    BuildTask task;
    task.kind = BuildTaskKind::Link;
    task.state = BuildTaskState::Pending;
    task.id = make_build_task_id(task.kind, executableNodeId);
    task.inputs = inputNodeIds;
    task.outputs.push_back(executableNodeId);
    task.command = command;
    task.commandHash = hash_build_command(command);
    task.workingDirectory = workingDirectory.lexically_normal();

    return task;
  }

  BuildTask make_archive_task(
      const std::vector<std::string> &inputNodeIds,
      const std::string &libraryNodeId,
      const std::vector<std::string> &command,
      const fs::path &workingDirectory)
  {
    BuildTask task;
    task.kind = BuildTaskKind::Archive;
    task.state = BuildTaskState::Pending;
    task.id = make_build_task_id(task.kind, libraryNodeId);
    task.inputs = inputNodeIds;
    task.outputs.push_back(libraryNodeId);
    task.command = command;
    task.commandHash = hash_build_command(command);
    task.workingDirectory = workingDirectory.lexically_normal();

    return task;
  }

} // namespace vix::cli::build
