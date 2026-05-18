/**
 *
 *  @file BuildGraphExecutor.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Target-aware build graph executor
 *
 */

#include <vix/cli/build/BuildGraphExecutor.hpp>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cctype>
#include <cstdlib>

#include <vix/cli/build/DependencyFile.hpp>
#include <vix/cli/build/ObjectCache.hpp>
#include <vix/cli/cmake/CMakeBuild.hpp>

namespace vix::cli::build
{
  namespace
  {
    static bool same_target_name(
        const fs::path &output,
        const std::string &target)
    {
      if (target.empty())
        return false;

      const fs::path targetPath(target);

      if (targetPath.is_absolute())
        return output.lexically_normal() == targetPath.lexically_normal();

      const std::string filename = output.filename().string();

#ifdef _WIN32
      if (filename == target)
        return true;

      if (filename == target + ".exe")
        return true;

      if (output.stem().string() == target)
        return true;
#else
      if (filename == target)
        return true;
#endif

      return false;
    }

    static BuildGraphExecutorResult make_failed_result(
        const BuildGraphExecutorOptions &options,
        BuildGraphExecutorStatus status,
        int exitCode,
        const std::string &reason)
    {
      BuildGraphExecutorResult result;
      result.ok = false;
      result.status = status;
      result.target = options.target;
      result.reason = reason;
      result.exitCode = exitCode;

      if (!reason.empty())
        result.output = reason + "\n";

      return result;
    }

    static std::unordered_map<std::string, std::string>
    make_output_to_task_map(const BuildGraph &graph)
    {
      std::unordered_map<std::string, std::string> out;

      for (const auto &kv : graph.tasks())
      {
        const BuildTask &task = kv.second;

        for (const std::string &outputId : task.outputs)
          out[outputId] = task.id;
      }

      return out;
    }

    static bool is_real_graph_target_task(const BuildTask &task)
    {
      return task.kind == BuildTaskKind::Link ||
             task.kind == BuildTaskKind::Archive;
    }

    static bool node_is_real_graph_output(const BuildNode &node)
    {
      return node.kind == BuildNodeKind::Executable ||
             node.kind == BuildNodeKind::Library;
    }

    static const BuildTask *find_target_task(
        const BuildGraph &graph,
        const std::string &target)
    {
      const BuildTask *match = nullptr;
      std::size_t matches = 0;

      for (const auto &kv : graph.tasks())
      {
        const BuildTask &task = kv.second;

        /*
         * Production policy:
         * Only real link/archive targets are executed through the graph path.
         * Generated, phony, copy and install targets are delegated to Ninja because
         * Ninja remains the authoritative executor for complex build semantics.
         */
        if (!is_real_graph_target_task(task))
          continue;

        for (const std::string &outputId : task.outputs)
        {
          const BuildNode *node = graph.find_node(outputId);
          if (!node)
            continue;

          if (!node_is_real_graph_output(*node))
            continue;

          if (!same_target_name(node->path, target))
            continue;

          match = &task;
          ++matches;
        }
      }

      if (matches != 1)
        return nullptr;

      return match;
    }

    static void collect_task_closure(
        const BuildGraph &graph,
        const std::string &taskId,
        const std::unordered_map<std::string, std::string> &outputToTask,
        std::unordered_set<std::string> &selected)
    {
      if (taskId.empty())
        return;

      if (!selected.insert(taskId).second)
        return;

      const BuildTask *task = graph.find_task(taskId);
      if (!task)
        return;

      for (const std::string &inputId : task->inputs)
      {
        const auto producerIt = outputToTask.find(inputId);
        if (producerIt == outputToTask.end())
          continue;

        collect_task_closure(
            graph,
            producerIt->second,
            outputToTask,
            selected);
      }

      for (const std::string &depTaskId : task->deps)
      {
        collect_task_closure(
            graph,
            depTaskId,
            outputToTask,
            selected);
      }
    }

    static std::vector<BuildTask> selected_compile_tasks(
        const BuildGraph &graph,
        const std::unordered_set<std::string> &selected)
    {
      std::vector<BuildTask> out;

      for (const std::string &taskId : selected)
      {
        const BuildTask *task = graph.find_task(taskId);
        if (!task)
          continue;

        if (task->kind == BuildTaskKind::Compile)
          out.push_back(*task);
      }

      std::sort(
          out.begin(),
          out.end(),
          [](const BuildTask &a, const BuildTask &b)
          {
            return a.id < b.id;
          });

      return out;
    }

    static std::vector<BuildTask> dirty_tasks_only(
        const BuildGraph &graph,
        const std::vector<BuildTask> &tasks)
    {
      std::vector<BuildTask> out;

      for (const BuildTask &task : tasks)
      {
        if (graph.task_is_dirty(task))
          out.push_back(task);
      }

      return out;
    }

    static bool graph_has_dirty_project_inputs(const BuildGraph &graph)
    {
      for (const auto &kv : graph.nodes())
      {
        const BuildNode &node = kv.second;

        if (!(node.dirty() || node.missing()))
          continue;

        if (node.kind == BuildNodeKind::Source ||
            node.kind == BuildNodeKind::Header ||
            node.kind == BuildNodeKind::Config)
        {
          return true;
        }
      }

      return false;
    }

    static bool collect_compile_task_paths(
        const BuildGraph &graph,
        const BuildTask &task,
        fs::path &sourcePath,
        fs::path &objectPath,
        std::vector<fs::path> &dependencyPaths)
    {
      sourcePath.clear();
      objectPath.clear();
      dependencyPaths.clear();

      for (const std::string &inputId : task.inputs)
      {
        const BuildNode *node = graph.find_node(inputId);
        if (!node)
          continue;

        if (node->kind == BuildNodeKind::Source && sourcePath.empty())
        {
          sourcePath = node->path;
          continue;
        }

        if (node->kind == BuildNodeKind::Header ||
            node->kind == BuildNodeKind::Config)
        {
          dependencyPaths.push_back(node->path);
        }
      }

      for (const std::string &outputId : task.outputs)
      {
        const BuildNode *node = graph.find_node(outputId);
        if (!node)
          continue;

        if (node->kind == BuildNodeKind::Object)
        {
          objectPath = node->path;
          break;
        }
      }

      return !sourcePath.empty() && !objectPath.empty();
    }

    static BuildTaskResult run_cached_compile_task(
        const BuildGraph &graph,
        const ObjectCache &objectCache,
        BuildTask &task)
    {
      BuildTaskResult result;
      result.taskId = task.id;

      fs::path sourcePath;
      fs::path objectPath;
      std::vector<fs::path> dependencyPaths;

      if (!collect_compile_task_paths(
              graph,
              task,
              sourcePath,
              objectPath,
              dependencyPaths))
      {
        result.state = BuildTaskState::Failed;
        result.exitCode = 127;
        result.output = "Invalid compile task: " + task.id + "\n";
        return result;
      }

      const fs::path dependencyFilePath =
          dependency_file_for_object(objectPath);

      const ObjectCacheResult restored =
          objectCache.resolve_compile_task(
              task,
              sourcePath,
              dependencyPaths,
              objectPath,
              dependencyFilePath,
              graph.config().buildFingerprint);

      if (restored.hit)
      {
        result.state = BuildTaskState::Skipped;
        result.exitCode = 0;
        result.output = "cache hit: " + sourcePath.string() + "\n";
        return result;
      }

      result = BuildScheduler::execute_command_task(task);

      if (result.exitCode != 0)
        return result;

      const std::string inputHash =
          ObjectCache::compute_input_hash(sourcePath, dependencyPaths);

      const std::string objectKey =
          ObjectCache::compute_object_key(
              sourcePath,
              inputHash,
              task.commandHash,
              graph.config().buildFingerprint);

      (void)objectCache.store(
          objectKey,
          sourcePath,
          objectPath,
          dependencyFilePath,
          inputHash,
          task.commandHash);

      return result;
    }

    static BuildTask make_ninja_target_task(
        const fs::path &buildDir,
        const std::string &target)
    {
      BuildTask task;
      task.id = "ninja-target:" + target;
      task.kind = BuildTaskKind::Link;
      task.state = BuildTaskState::Pending;
      task.workingDirectory = buildDir;

      task.command = {
          "ninja",
          "-C",
          buildDir.string(),
          target};

      return task;
    }

    static BuildGraphExecutorResult run_ninja_target_fallback(
        const BuildGraphExecutorOptions &options,
        BuildGraphExecutorStatus status,
        const std::string &reason)
    {
      if (!options.allowNinjaFallback)
      {
        return make_failed_result(
            options,
            status,
            2,
            reason);
      }

      BuildGraphExecutorResult result;
      result.target = options.target;
      result.status = BuildGraphExecutorStatus::DelegatedToNinja;
      result.reason = reason;
      result.usedNinja = true;
      result.usedFallback = true;

      BuildTask ninjaTargetTask =
          make_ninja_target_task(
              options.buildDir,
              options.target);

      const process::ExecResult ninjaResult =
          run_process_live_to_log(
              ninjaTargetTask.command,
              {},
              options.buildDir / "build.log",
              options.quiet,
              /*cmakeVerbose=*/false,
              /*progressOnly=*/false);

      result.exitCode = ninjaResult.exitCode;

      if (!reason.empty())
        result.output = reason + "\n";

      if (ninjaResult.producedOutput && !ninjaResult.capturedFirstLine.empty())
        result.output += ninjaResult.capturedFirstLine + "\n";

      if (ninjaResult.exitCode == 0)
      {
        result.ok = true;
        return result;
      }

      result.ok = false;
      result.status = BuildGraphExecutorStatus::NinjaFailed;

      if (!ninjaResult.displayCommand.empty())
        result.output += ninjaResult.displayCommand + "\n";

      return result;
    }

    static bool graph_debug_logs_enabled()
    {
      const char *level = std::getenv("VIX_LOG_LEVEL");

      if (!level || !*level)
        return false;

      std::string value(level);

      for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      return value == "debug" || value == "trace";
    }

    static void graph_log(
        const BuildGraphExecutorOptions &options,
        const std::string &message)
    {
      if (options.quiet)
        return;

      if (!options.verbose)
        return;

      if (!graph_debug_logs_enabled())
        return;

      std::cout << "  " << message << "\n";
      std::cout.flush();
    }
  } // namespace

  BuildGraphExecutor::BuildGraphExecutor(BuildGraphExecutorOptions options)
      : options_(std::move(options))
  {
  }

  const BuildGraphExecutorOptions &BuildGraphExecutor::options() const
  {
    return options_;
  }

  BuildGraphExecutorResult BuildGraphExecutor::run_target(BuildGraph &graph) const
  {
    BuildGraphExecutorResult result;
    result.target = options_.target;

    graph_log(
        options_,
        "graph: starting target executor for `" + options_.target + "`");

    if (options_.target.empty())
    {
      result.ok = false;
      result.exitCode = 2;
      result.output = "Missing graph build target.\n";
      return result;
    }

    graph_log(options_, "graph: resolving target task");

    const BuildTask *targetTask =
        find_target_task(graph, options_.target);

    if (!targetTask)
    {
      return run_ninja_target_fallback(
          options_,
          BuildGraphExecutorStatus::UnsupportedTarget,
          "Unable to resolve a unique graph output target: " + options_.target);
    }

    graph_log(
        options_,
        "graph: target task resolved: " + targetTask->id);

    graph_log(options_, "graph: building output-to-task map");

    const auto outputToTask = make_output_to_task_map(graph);

    graph_log(
        options_,
        "graph: collecting task closure");

    std::unordered_set<std::string> selected;
    collect_task_closure(
        graph,
        targetTask->id,
        outputToTask,
        selected);

    result.selectedTasks = selected.size();

    graph_log(
        options_,
        "graph: selected " + std::to_string(result.selectedTasks) + " tasks");

    graph_log(
        options_,
        "graph: collecting compile tasks");

    const std::vector<BuildTask> compileTasks =
        selected_compile_tasks(graph, selected);

    graph_log(
        options_,
        "graph: selected " + std::to_string(compileTasks.size()) +
            " compile tasks");

    graph_log(
        options_,
        "graph: checking dirty compile tasks");

    const std::vector<BuildTask> dirtyCompileTasks =
        dirty_tasks_only(graph, compileTasks);

    result.selectedCompileTasks = compileTasks.size();
    result.dirtyCompileTasks = dirtyCompileTasks.size();
    bool delegatedToNinjaBecauseDirtyProjectInput = false;

    graph_log(
        options_,
        "graph: dirty compile tasks: " +
            std::to_string(result.dirtyCompileTasks));

    if (options_.maxGraphDirtyCompileTasks > 0 &&
        result.dirtyCompileTasks > options_.maxGraphDirtyCompileTasks)
    {
      return run_ninja_target_fallback(
          options_,
          BuildGraphExecutorStatus::DelegatedToNinja,
          "Graph target has too many dirty compile tasks: " +
              std::to_string(result.dirtyCompileTasks) +
              " dirty tasks from " +
              std::to_string(result.selectedCompileTasks) +
              " selected compile tasks. Delegating to Ninja.");
    }

    if (!dirtyCompileTasks.empty())
    {
      ObjectCache objectCache(options_.buildDir);

      if (!objectCache.ensure_layout())
      {
        result.ok = false;
        result.exitCode = 1;
        result.output = "Unable to initialize object cache.\n";
        return result;
      }

      BuildSchedulerOptions schedulerOptions;
      schedulerOptions.jobs = options_.jobs;
      schedulerOptions.quiet = true;
      schedulerOptions.stopOnFirstFailure = true;

      BuildScheduler scheduler(schedulerOptions);
      scheduler.add_tasks(dirtyCompileTasks);

      const BuildSchedulerResult compileResult =
          scheduler.run(
              [&](BuildTask &task)
              {
                graph_log(
                    options_,
                    "graph: compile " + task.id);

                return run_cached_compile_task(
                    graph,
                    objectCache,
                    task);
              });

      result.executedCompileTasks = compileResult.done;
      result.skippedCompileTasks = compileResult.skipped;

      for (const BuildTaskResult &taskResult : compileResult.results)
      {
        if (!taskResult.output.empty())
          result.output += taskResult.output;
      }

      if (!compileResult.success())
      {
        result.ok = false;
        result.exitCode = 1;
        return result;
      }
    }
    else
    {
      graph_log(options_, "graph: no dirty compile tasks");
    }

    if (dirtyCompileTasks.empty())
    {
      const bool hasDirtyProjectInputs =
          graph_has_dirty_project_inputs(graph);

      bool outputsExist = true;

      for (const std::string &outputId : targetTask->outputs)
      {
        const BuildNode *node = graph.find_node(outputId);

        if (!node || node->missing())
        {
          outputsExist = false;
          break;
        }
      }

      if (outputsExist && !hasDirtyProjectInputs)
      {
        result.ok = true;
        result.exitCode = 0;
        result.executedCompileTasks = 0;
        result.skippedCompileTasks = result.selectedCompileTasks;
        result.output += "Graph target is up to date.\n";

        graph_log(options_, "graph: target outputs exist, skipping ninja");

        return result;
      }

      if (hasDirtyProjectInputs)
      {
        delegatedToNinjaBecauseDirtyProjectInput = true;

        graph_log(
            options_,
            "graph: dirty project input detected, delegating target to ninja");
      }
    }

    graph_log(
        options_,
        "graph: running ninja target `" + options_.target + "`");

    BuildTask ninjaTargetTask =
        make_ninja_target_task(
            options_.buildDir,
            options_.target);

    const process::ExecResult ninjaResult =
        run_process_live_to_log(
            ninjaTargetTask.command,
            {},
            options_.buildDir / "build.log",
            options_.quiet,
            /*cmakeVerbose=*/false,
            /*progressOnly=*/false);

    result.exitCode = ninjaResult.exitCode;
    result.ok = ninjaResult.exitCode == 0;

    if (result.ok && delegatedToNinjaBecauseDirtyProjectInput)
    {
      result.dirtyCompileTasks = 1;
    }

    graph_log(
        options_,
        "graph: ninja target finished with exit code " +
            std::to_string(result.exitCode));

    if (!result.ok)
    {
      result.output += "Ninja target failed: " + options_.target + "\n";
      result.output += ninjaResult.displayCommand + "\n";
    }

    return result;
  }

} // namespace vix::cli::build
