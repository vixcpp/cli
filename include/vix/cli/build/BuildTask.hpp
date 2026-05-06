/**
 *
 *  @file BuildTask.hpp
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

#ifndef VIX_CLI_BUILD_BUILD_TASK_HPP
#define VIX_CLI_BUILD_BUILD_TASK_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  /**
   * @brief Type of work represented by a build task.
   *
   * A BuildTask is an executable action derived from the build graph.
   * It does not describe a file itself. Files are represented by BuildNode.
   */
  enum class BuildTaskKind
  {
    Unknown, ///< Unknown or unsupported task
    Compile, ///< Compile one source file into one object file
    Link,    ///< Link object files into an executable
    Archive, ///< Archive object files into a static library
    Copy,    ///< Copy a generated artifact
    Generate ///< Generate a file before compilation
  };

  /**
   * @brief Runtime state of a build task.
   *
   * The scheduler uses this state to know which tasks are ready, running,
   * done, failed, or skipped because the cache already contains the output.
   */
  enum class BuildTaskState
  {
    Pending, ///< Task is waiting for dependencies
    Ready,   ///< Task can be executed
    Running, ///< Task is currently executing
    Done,    ///< Task completed successfully
    Failed,  ///< Task failed
    Skipped  ///< Task output was reused from cache
  };

  /**
   * @brief Convert a build task kind to a stable string.
   *
   * @param kind Task kind
   * @return Stable lowercase string representation
   */
  std::string to_string(BuildTaskKind kind);

  /**
   * @brief Convert a build task state to a stable string.
   *
   * @param state Task state
   * @return Stable lowercase string representation
   */
  std::string to_string(BuildTaskState state);

  /**
   * @brief Parse a build task kind from a stable string.
   *
   * Unknown strings are parsed as BuildTaskKind::Unknown.
   *
   * @param value String value
   * @return Parsed task kind
   */
  BuildTaskKind build_task_kind_from_string(const std::string &value);

  /**
   * @brief Parse a build task state from a stable string.
   *
   * Unknown strings are parsed as BuildTaskState::Pending.
   *
   * @param value String value
   * @return Parsed task state
   */
  BuildTaskState build_task_state_from_string(const std::string &value);

  /**
   * @brief One executable task inside the Vix build plan.
   *
   * A task describes work to perform:
   *   - compile source to object
   *   - archive objects into a library
   *   - link objects into an executable
   *   - copy or generate files
   *
   * Each task has:
   *   - a stable id
   *   - input node ids
   *   - output node ids
   *   - dependency task ids
   *   - command arguments
   *   - a command hash
   *
   * The command hash is important for cache correctness. If compiler flags,
   * defines, include paths, target triple, or linker flags change, the task
   * must be considered dirty even if the source file did not change.
   */
  struct BuildTask
  {
    std::string id; ///< Stable task id

    BuildTaskKind kind{BuildTaskKind::Unknown};    ///< Type of task
    BuildTaskState state{BuildTaskState::Pending}; ///< Runtime task state

    std::vector<std::string> inputs;  ///< Input build node ids
    std::vector<std::string> outputs; ///< Output build node ids
    std::vector<std::string> deps;    ///< Dependency task ids

    std::vector<std::string> command; ///< Command argv, no shell required
    std::string commandHash;          ///< Stable hash of command + relevant config

    fs::path workingDirectory; ///< Directory where the command should run
    fs::path logFile;          ///< Optional log file for the task output

    std::uint64_t startedUnixMs{0};  ///< Start timestamp in milliseconds
    std::uint64_t finishedUnixMs{0}; ///< Finish timestamp in milliseconds
    int exitCode{0};                 ///< Process exit code

    /**
     * @brief Check whether this task has a valid id.
     *
     * @return true if id is not empty
     */
    bool valid() const;

    /**
     * @brief Check whether the task is pending.
     *
     * @return true if state is BuildTaskState::Pending
     */
    bool pending() const;

    /**
     * @brief Check whether the task is ready.
     *
     * @return true if state is BuildTaskState::Ready
     */
    bool ready() const;

    /**
     * @brief Check whether the task is running.
     *
     * @return true if state is BuildTaskState::Running
     */
    bool running() const;

    /**
     * @brief Check whether the task is done.
     *
     * @return true if state is BuildTaskState::Done
     */
    bool done() const;

    /**
     * @brief Check whether the task failed.
     *
     * @return true if state is BuildTaskState::Failed
     */
    bool failed() const;

    /**
     * @brief Check whether the task was skipped because of a cache hit.
     *
     * @return true if state is BuildTaskState::Skipped
     */
    bool skipped() const;

    /**
     * @brief Mark this task as pending.
     */
    void mark_pending();

    /**
     * @brief Mark this task as ready.
     */
    void mark_ready();

    /**
     * @brief Mark this task as running.
     */
    void mark_running();

    /**
     * @brief Mark this task as done.
     */
    void mark_done();

    /**
     * @brief Mark this task as failed.
     *
     * @param code Process exit code
     */
    void mark_failed(int code);

    /**
     * @brief Mark this task as skipped.
     */
    void mark_skipped();

    /**
     * @brief Add an input node id if it is not already present.
     *
     * Empty input ids are ignored.
     *
     * @param nodeId Input node id
     */
    void add_input(const std::string &nodeId);

    /**
     * @brief Add an output node id if it is not already present.
     *
     * Empty output ids are ignored.
     *
     * @param nodeId Output node id
     */
    void add_output(const std::string &nodeId);

    /**
     * @brief Add a dependency task id if it is not already present.
     *
     * Empty dependency ids are ignored.
     *
     * @param taskId Dependency task id
     */
    void add_dependency(const std::string &taskId);

    /**
     * @brief Check whether this task has an input node id.
     *
     * @param nodeId Input node id
     * @return true if present
     */
    bool has_input(const std::string &nodeId) const;

    /**
     * @brief Check whether this task has an output node id.
     *
     * @param nodeId Output node id
     * @return true if present
     */
    bool has_output(const std::string &nodeId) const;

    /**
     * @brief Check whether this task depends on another task id.
     *
     * @param taskId Dependency task id
     * @return true if present
     */
    bool has_dependency(const std::string &taskId) const;
  };

  /**
   * @brief Create a stable task id from task kind and output node id.
   *
   * @param kind Task kind
   * @param outputId Primary output node id
   * @return Stable task id
   */
  std::string make_build_task_id(
      BuildTaskKind kind,
      const std::string &outputId);

  /**
   * @brief Compute a stable hash for a command argv vector.
   *
   * This is used to detect changes in compiler flags, include paths, defines,
   * target triple, sanitizer flags, linker flags, and other build options.
   *
   * @param command Command argv vector
   * @return Stable hexadecimal hash
   */
  std::string hash_build_command(const std::vector<std::string> &command);

  /**
   * @brief Create a compile task.
   *
   * @param sourceNodeId Source node id
   * @param objectNodeId Object node id
   * @param command Compiler command argv
   * @param workingDirectory Working directory
   * @return Build task
   */
  BuildTask make_compile_task(
      const std::string &sourceNodeId,
      const std::string &objectNodeId,
      const std::vector<std::string> &command,
      const fs::path &workingDirectory);

  /**
   * @brief Create a link task.
   *
   * @param inputNodeIds Object/library node ids
   * @param executableNodeId Executable output node id
   * @param command Linker command argv
   * @param workingDirectory Working directory
   * @return Build task
   */
  BuildTask make_link_task(
      const std::vector<std::string> &inputNodeIds,
      const std::string &executableNodeId,
      const std::vector<std::string> &command,
      const fs::path &workingDirectory);

  /**
   * @brief Create an archive task.
   *
   * @param inputNodeIds Object node ids
   * @param libraryNodeId Library output node id
   * @param command Archiver command argv
   * @param workingDirectory Working directory
   * @return Build task
   */
  BuildTask make_archive_task(
      const std::vector<std::string> &inputNodeIds,
      const std::string &libraryNodeId,
      const std::vector<std::string> &command,
      const fs::path &workingDirectory);

} // namespace vix::cli::build

#endif
