/**
 *
 *  @file BuildScheduler.cpp
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

#include <vix/cli/build/BuildScheduler.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <filesystem>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  namespace
  {
    static std::string shell_quote(const std::string &value)
    {
#ifdef _WIN32
      std::string out = "\"";
      for (char c : value)
      {
        if (c == '"')
          out += "\"\"";
        else
          out.push_back(c);
      }
      out += "\"";
      return out;
#else
      std::string out = "'";
      for (char c : value)
      {
        if (c == '\'')
          out += "'\\''";
        else
          out.push_back(c);
      }
      out += "'";
      return out;
#endif
    }

    static std::string command_to_shell_string(const std::vector<std::string> &command)
    {
      std::ostringstream out;

      for (std::size_t i = 0; i < command.size(); ++i)
      {
        if (i > 0)
          out << " ";

        out << shell_quote(command[i]);
      }

      return out.str();
    }

    static std::string command_to_shell_string_with_working_directory(
        const std::vector<std::string> &command,
        const fs::path &workingDirectory)
    {
      std::ostringstream out;

#ifndef _WIN32
      if (!workingDirectory.empty())
      {
        out << "cd ";
        out << shell_quote(workingDirectory.string());
        out << " && ";
      }
#else
      if (!workingDirectory.empty())
      {
        out << "cd /d ";
        out << shell_quote(workingDirectory.string());
        out << " && ";
      }
#endif

      out << command_to_shell_string(command);
      return out.str();
    }

    static int normalize_exit_code(int code)
    {
#ifdef _WIN32
      return code;
#else
      if (WIFEXITED(code))
        return WEXITSTATUS(code);

      if (WIFSIGNALED(code))
        return 128 + WTERMSIG(code);

      return code;
#endif
    }

    static bool is_terminal_state(BuildTaskState state)
    {
      return state == BuildTaskState::Done ||
             state == BuildTaskState::Skipped ||
             state == BuildTaskState::Failed;
    }
  } // namespace

  bool BuildSchedulerResult::success() const
  {
    return failed == 0;
  }

  BuildScheduler::BuildScheduler(BuildSchedulerOptions options)
      : options_(options)
  {
  }

  void BuildScheduler::set_options(BuildSchedulerOptions options)
  {
    options_ = options;
  }

  const BuildSchedulerOptions &BuildScheduler::options() const
  {
    return options_;
  }

  void BuildScheduler::clear()
  {
    tasks_.clear();
  }

  bool BuildScheduler::add_task(const BuildTask &task)
  {
    if (!task.valid())
      return false;

    tasks_[task.id] = task;
    return true;
  }

  void BuildScheduler::add_tasks(const std::vector<BuildTask> &tasks)
  {
    for (const auto &task : tasks)
      add_task(task);
  }

  BuildTask *BuildScheduler::find_task(const std::string &id)
  {
    const auto it = tasks_.find(id);
    if (it == tasks_.end())
      return nullptr;

    return &it->second;
  }

  const BuildTask *BuildScheduler::find_task(const std::string &id) const
  {
    const auto it = tasks_.find(id);
    if (it == tasks_.end())
      return nullptr;

    return &it->second;
  }

  const std::unordered_map<std::string, BuildTask> &BuildScheduler::tasks() const
  {
    return tasks_;
  }

  std::vector<std::string> BuildScheduler::sorted_task_ids() const
  {
    std::vector<std::string> ids;
    ids.reserve(tasks_.size());

    for (const auto &kv : tasks_)
      ids.push_back(kv.first);

    std::sort(ids.begin(), ids.end());
    return ids;
  }

  bool BuildScheduler::dependencies_complete(const BuildTask &task) const
  {
    for (const auto &depId : task.deps)
    {
      const BuildTask *dep = find_task(depId);

      if (!dep)
        return false;

      if (!task_finished_successfully(depId))
        return false;
    }

    return true;
  }

  bool BuildScheduler::has_missing_dependencies() const
  {
    return !missing_dependencies().empty();
  }

  std::vector<std::string> BuildScheduler::missing_dependencies() const
  {
    std::vector<std::string> missing;

    for (const auto &kv : tasks_)
    {
      const BuildTask &task = kv.second;

      for (const auto &depId : task.deps)
      {
        if (!find_task(depId))
          missing.push_back(depId);
      }
    }

    std::sort(missing.begin(), missing.end());
    missing.erase(std::unique(missing.begin(), missing.end()), missing.end());

    return missing;
  }

  BuildSchedulerResult BuildScheduler::run(const BuildTaskExecutor &executor)
  {
    BuildSchedulerResult summary;
    summary.total = tasks_.size();

    if (!executor)
    {
      summary.failed = tasks_.size();

      for (auto &kv : tasks_)
      {
        kv.second.mark_failed(127);

        BuildTaskResult result;
        result.taskId = kv.first;
        result.state = BuildTaskState::Failed;
        result.exitCode = 127;
        result.output = "No build task executor provided.";

        summary.results.push_back(std::move(result));
      }

      return summary;
    }

    const auto missing = missing_dependencies();
    if (!missing.empty())
    {
      summary.failed = tasks_.size();

      std::ostringstream msg;
      msg << "Missing task dependencies:";

      for (const auto &dep : missing)
        msg << " " << dep;

      for (auto &kv : tasks_)
      {
        kv.second.mark_failed(127);

        BuildTaskResult result;
        result.taskId = kv.first;
        result.state = BuildTaskState::Failed;
        result.exitCode = 127;
        result.output = msg.str();

        summary.results.push_back(std::move(result));
      }

      return summary;
    }

    int jobs = options_.jobs;
    if (jobs <= 0)
      jobs = default_jobs();

    if (jobs <= 0)
      jobs = 1;

    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::string> queue;
    std::vector<BuildTaskResult> results;

    bool stopRequested = false;
    bool schedulingDone = false;

    std::size_t running = 0;
    std::size_t finished = 0;

    auto schedule_ready_tasks = [&]()
    {
      const auto ready = ready_task_ids();

      for (const auto &taskId : ready)
      {
        BuildTask *task = find_task(taskId);
        if (!task)
          continue;

        if (!task->pending())
          continue;

        task->mark_ready();
        queue.push_back(taskId);
      }
    };

    auto worker = [&]()
    {
      while (true)
      {
        std::string taskId;

        {
          std::unique_lock<std::mutex> lock(mutex);

          cv.wait(
              lock,
              [&]()
              {
                return !queue.empty() || schedulingDone;
              });

          if (queue.empty() && schedulingDone)
            return;

          if (queue.empty())
            continue;

          taskId = queue.front();
          queue.pop_front();

          BuildTask *task = find_task(taskId);
          if (!task)
            continue;

          if (!task->ready())
            continue;

          task->mark_running();
          ++running;
        }

        BuildTaskResult result;

        {
          BuildTask taskCopy;

          {
            std::lock_guard<std::mutex> lock(mutex);
            BuildTask *task = find_task(taskId);

            if (!task)
            {
              result.taskId = taskId;
              result.state = BuildTaskState::Failed;
              result.exitCode = 127;
              result.output = "Task disappeared before execution.";
            }
            else
            {
              taskCopy = *task;
            }
          }

          if (result.taskId.empty())
            result = executor(taskCopy);
        }

        {
          std::lock_guard<std::mutex> lock(mutex);

          if (result.taskId.empty())
            result.taskId = taskId;

          apply_result(result);
          results.push_back(result);

          --running;
          ++finished;

          if (result.state == BuildTaskState::Failed &&
              options_.stopOnFirstFailure)
          {
            stopRequested = true;
          }

          if (!options_.quiet && !result.output.empty())
            std::cout << result.output;

          if (!stopRequested)
            schedule_ready_tasks();

          if (finished >= tasks_.size() || stopRequested)
            schedulingDone = true;
        }

        cv.notify_all();
      }
    };

    {
      std::lock_guard<std::mutex> lock(mutex);
      schedule_ready_tasks();

      if (queue.empty() && tasks_.empty())
        schedulingDone = true;

      if (queue.empty() && !tasks_.empty())
      {
        bool allTerminal = true;

        for (const auto &kv : tasks_)
        {
          if (!is_terminal_state(kv.second.state))
          {
            allTerminal = false;
            break;
          }
        }

        if (allTerminal)
          schedulingDone = true;
      }
    }

    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(jobs));

    for (int i = 0; i < jobs; ++i)
      workers.emplace_back(worker);

    {
      std::unique_lock<std::mutex> lock(mutex);

      cv.wait(
          lock,
          [&]()
          {
            if (schedulingDone)
              return true;

            if (finished >= tasks_.size())
              return true;

            if (stopRequested)
              return true;

            return false;
          });

      schedulingDone = true;
    }

    cv.notify_all();

    for (auto &t : workers)
    {
      if (t.joinable())
        t.join();
    }

    summary.results = std::move(results);

    for (const auto &kv : tasks_)
    {
      const BuildTask &task = kv.second;

      if (task.done())
        ++summary.done;
      else if (task.skipped())
        ++summary.skipped;
      else if (task.failed())
        ++summary.failed;
      else if (!task_finished_successfully(task.id))
        ++summary.failed;
    }

    return summary;
  }

  BuildTaskResult BuildScheduler::execute_command_task(BuildTask &task)
  {
    BuildTaskResult result;
    result.taskId = task.id;

    if (task.command.empty())
    {
      result.state = BuildTaskState::Failed;
      result.exitCode = 127;
      result.output = "Empty build command for task: " + task.id + "\n";
      return result;
    }

    const std::string shellCommand =
        command_to_shell_string_with_working_directory(
            task.command,
            task.workingDirectory) +
        " 2>&1";

#ifdef _WIN32
    FILE *pipe = _popen(shellCommand.c_str(), "r");
#else
    FILE *pipe = popen(shellCommand.c_str(), "r");
#endif

    if (!pipe)
    {
      result.state = BuildTaskState::Failed;
      result.exitCode = 127;
      result.output = "Failed to start task: " + task.id + "\n";
      return result;
    }

    std::ostringstream output;
    char buffer[4096];

    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
      output << buffer;

#ifdef _WIN32
    const int rawCode = _pclose(pipe);
#else
    const int rawCode = pclose(pipe);
#endif

    const int exitCode = normalize_exit_code(rawCode);

    result.exitCode = exitCode;
    result.output = output.str();

    if (exitCode == 0)
      result.state = BuildTaskState::Done;
    else
      result.state = BuildTaskState::Failed;

    return result;
  }

  int BuildScheduler::default_jobs()
  {
    unsigned int hc = std::thread::hardware_concurrency();

    if (hc == 0)
      return 4;

    if (hc > 64)
      hc = 64;

    return static_cast<int>(hc);
  }

  bool BuildScheduler::task_finished_successfully(const std::string &taskId) const
  {
    const BuildTask *task = find_task(taskId);

    if (!task)
      return false;

    return task->done() || task->skipped();
  }

  std::vector<std::string> BuildScheduler::ready_task_ids() const
  {
    std::vector<std::string> ids;

    for (const auto &kv : tasks_)
    {
      const BuildTask &task = kv.second;

      if (!task.pending())
        continue;

      if (!dependencies_complete(task))
        continue;

      ids.push_back(task.id);
    }

    std::sort(ids.begin(), ids.end());
    return ids;
  }

  void BuildScheduler::apply_result(const BuildTaskResult &result)
  {
    BuildTask *task = find_task(result.taskId);

    if (!task)
      return;

    if (result.state == BuildTaskState::Done)
    {
      task->mark_done();
      return;
    }

    if (result.state == BuildTaskState::Skipped)
    {
      task->mark_skipped();
      return;
    }

    if (result.state == BuildTaskState::Failed)
    {
      task->mark_failed(result.exitCode);
      return;
    }

    if (result.exitCode == 0)
      task->mark_done();
    else
      task->mark_failed(result.exitCode);
  }

} // namespace vix::cli::build
