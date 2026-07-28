#include <vix/cli/build/BuildTaskProcessExecutor.hpp>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vix::cli::build;

namespace
{
  namespace fs = std::filesystem;

  struct TempDir
  {
    fs::path path;

    TempDir()
    {
      path = fs::temp_directory_path() /
             ("vix-process-executor-test-" + std::to_string(std::rand()));
      fs::remove_all(path);
      fs::create_directories(path);
    }

    ~TempDir()
    {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
  };

  static void require(bool condition, const std::string &message)
  {
    if (!condition)
      throw std::runtime_error(message);
  }

  static BuildTask command_task(std::string id, std::vector<std::string> command)
  {
    BuildTask task;
    task.id = std::move(id);
    task.kind = BuildTaskKind::Compile;
    task.command = std::move(command);
    return task;
  }

#ifdef _WIN32
  static std::vector<std::string> shell_command(const std::string &script)
  {
    return {"cmd", "/C", script};
  }
#else
  static std::vector<std::string> shell_command(const std::string &script)
  {
    return {"sh", "-c", script};
  }
#endif

  static void test_successful_command()
  {
    BuildTask task = command_task("success", shell_command("printf success"));
    const BuildTaskResult result = execute_build_task_process(task);

    require(result.taskId == "success", "success task id");
    require(result.state == BuildTaskState::Done, "success state");
    require(result.exitCode == 0, "success exit code");
    require(result.output == "success", "success output");
  }

  static void test_non_zero_exit_code()
  {
#ifdef _WIN32
    BuildTask task = command_task("fail", shell_command("exit /B 7"));
#else
    BuildTask task = command_task("fail", shell_command("exit 7"));
#endif
    const BuildTaskResult result = execute_build_task_process(task);

    require(result.state == BuildTaskState::Failed, "failure state");
    require(result.exitCode == 7, "failure exit code normalized");
  }

  static void test_stdout_stderr_capture()
  {
#ifdef _WIN32
    BuildTask task = command_task("capture", shell_command("echo out& echo err 1>&2"));
#else
    BuildTask task = command_task("capture", shell_command("printf out; printf err >&2"));
#endif
    const BuildTaskResult result = execute_build_task_process(task);

    require(result.state == BuildTaskState::Done, "capture state");
    require(result.output.find("out") != std::string::npos, "stdout captured");
    require(result.output.find("err") != std::string::npos, "stderr captured");
  }

  static void test_working_directory()
  {
    TempDir temp;
    BuildTask task = command_task("pwd", shell_command(
#ifdef _WIN32
        "cd"
#else
        "pwd"
#endif
        ));
    task.workingDirectory = temp.path;

    const BuildTaskResult result = execute_build_task_process(task);

    require(result.state == BuildTaskState::Done, "working directory state");
    require(result.output.find(temp.path.string()) != std::string::npos, "working directory output");
  }

  static void test_arguments_containing_spaces()
  {
#ifdef _WIN32
    BuildTask task = command_task("spaces", {"cmd", "/C", "echo hello world"});
#else
    BuildTask task = command_task("spaces", {"sh", "-c", "printf '%s' \"$1\"", "_", "hello world"});
#endif
    const BuildTaskResult result = execute_build_task_process(task);

    require(result.state == BuildTaskState::Done, "spaces state");
    require(result.output.find("hello world") != std::string::npos, "spaces preserved");
  }

  static void test_empty_command()
  {
    BuildTask task = command_task("empty", {});
    const BuildTaskResult result = execute_build_task_process(task);

    require(result.state == BuildTaskState::Failed, "empty command state");
    require(result.exitCode == 127, "empty command exit");
    require(result.output.find("Empty build command") != std::string::npos, "empty command output");
  }
} // namespace

int main()
{
  try
  {
    test_successful_command();
    test_non_zero_exit_code();
    test_stdout_stderr_capture();
    test_working_directory();
    test_arguments_containing_spaces();
    test_empty_command();
  }
  catch (const std::exception &ex)
  {
    std::cerr << "BuildTaskProcessExecutorTests failed: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
