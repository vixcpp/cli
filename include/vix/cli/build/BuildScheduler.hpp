/**
 *
 *  @file BuildScheduler.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Parallel build task scheduler
 *
 */

#ifndef VIX_CLI_BUILD_BUILD_SCHEDULER_HPP
#define VIX_CLI_BUILD_BUILD_SCHEDULER_HPP

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <vix/cli/build/BuildTask.hpp>

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  /**
   * @brief Result produced after running a build task.
   */
  struct BuildTaskResult
  {
    std::string taskId;                            ///< Task id
    BuildTaskState state{BuildTaskState::Pending}; ///< Final task state
    int exitCode{0};                               ///< Process exit code
    std::string output;                            ///< Captured stdout/stderr
  };

  /**
   * @brief Summary produced after scheduler execution.
   */
  struct BuildSchedulerResult
  {
    std::size_t total{0};   ///< Total number of tasks
    std::size_t done{0};    ///< Successfully executed tasks
    std::size_t skipped{0}; ///< Skipped tasks
    std::size_t failed{0};  ///< Failed tasks

    std::vector<BuildTaskResult> results; ///< Per-task results

    /**
     * @brief Check whether all tasks completed successfully.
     *
     * @return true if no task failed
     */
    bool success() const;
  };

  /**
   * @brief Options used by the build scheduler.
   */
  struct BuildSchedulerOptions
  {
    int jobs{0};                   ///< Number of parallel jobs. 0 means auto.
    bool quiet{false};             ///< Suppress live output
    bool stopOnFirstFailure{true}; ///< Stop scheduling new tasks after first failure
  };

  /**
   * @brief Function used to execute one build task.
   *
   * This allows the scheduler to stay generic. Later, Vix can plug:
   *   - a real process executor
   *   - an object cache restore path
   *   - a dry-run executor
   *   - a test executor
   */
  using BuildTaskExecutor = std::function<BuildTaskResult(BuildTask &task)>;

  /**
   * @brief Parallel scheduler for Vix build tasks.
   *
   * BuildScheduler executes BuildTask objects according to dependency edges.
   *
   * A task can run when:
   *   - it is pending or ready
   *   - all dependency task ids are done or skipped
   *
   * Independent tasks are executed in parallel. This is the foundation for
   * Zig-like deep parallel builds in Vix.
   */
  class BuildScheduler
  {
  public:
    /**
     * @brief Create an empty scheduler.
     */
    BuildScheduler() = default;

    /**
     * @brief Create a scheduler with options.
     *
     * @param options Scheduler options
     */
    explicit BuildScheduler(BuildSchedulerOptions options);

    /**
     * @brief Set scheduler options.
     *
     * @param options Scheduler options
     */
    void set_options(BuildSchedulerOptions options);

    /**
     * @brief Return scheduler options.
     *
     * @return Scheduler options
     */
    const BuildSchedulerOptions &options() const;

    /**
     * @brief Remove all registered tasks.
     */
    void clear();

    /**
     * @brief Add or replace a task.
     *
     * @param task Build task
     * @return true if the task was valid and stored
     */
    bool add_task(const BuildTask &task);

    /**
     * @brief Add multiple tasks.
     *
     * @param tasks Build tasks
     */
    void add_tasks(const std::vector<BuildTask> &tasks);

    /**
     * @brief Find a task by id.
     *
     * @param id Task id
     * @return Task pointer, or nullptr if missing
     */
    BuildTask *find_task(const std::string &id);

    /**
     * @brief Find a task by id.
     *
     * @param id Task id
     * @return Task pointer, or nullptr if missing
     */
    const BuildTask *find_task(const std::string &id) const;

    /**
     * @brief Return all tasks.
     *
     * @return Task map
     */
    const std::unordered_map<std::string, BuildTask> &tasks() const;

    /**
     * @brief Return task ids in deterministic order.
     *
     * @return Sorted task ids
     */
    std::vector<std::string> sorted_task_ids() const;

    /**
     * @brief Check whether all dependencies of a task are completed.
     *
     * Dependencies are considered complete when they are Done or Skipped.
     *
     * @param task Task to inspect
     * @return true if all dependency tasks are complete
     */
    bool dependencies_complete(const BuildTask &task) const;

    /**
     * @brief Check whether the graph has missing dependency task ids.
     *
     * @return true if at least one task references a missing dependency
     */
    bool has_missing_dependencies() const;

    /**
     * @brief Return missing dependency task ids.
     *
     * @return Missing dependency ids
     */
    std::vector<std::string> missing_dependencies() const;

    /**
     * @brief Run all tasks using the provided executor.
     *
     * The scheduler manages dependency readiness and parallel execution.
     * The executor only runs one task.
     *
     * @param executor Task executor
     * @return Scheduler result
     */
    BuildSchedulerResult run(const BuildTaskExecutor &executor);

    /**
     * @brief Execute one command task using fork/exec on Unix or system fallback.
     *
     * This helper is a default executor for BuildTask::command.
     * It captures stdout/stderr into BuildTaskResult::output.
     *
     * @param task Build task
     * @return Task result
     */
    static BuildTaskResult execute_command_task(BuildTask &task);

    /**
     * @brief Return the default number of jobs.
     *
     * @return Number of jobs
     */
    static int default_jobs();

  private:
    BuildSchedulerOptions options_{};
    std::unordered_map<std::string, BuildTask> tasks_;

    bool task_finished_successfully(const std::string &taskId) const;
    std::vector<std::string> ready_task_ids() const;
    void apply_result(const BuildTaskResult &result);
  };

} // namespace vix::cli::build

#endif
