#include <vix/cli/build/BuildTaskProcessExecutor.hpp>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vix::cli::build;

namespace
{
  namespace fs = std::filesystem;

  static fs::path selfPath;

  struct TempDir
  {
    fs::path path;

    TempDir()
    {
      path = fs::temp_directory_path() /
             ("vix-build-task-process-adapter-test-" + std::to_string(std::rand()));
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

  static std::vector<std::string> self_command(std::vector<std::string> args)
  {
    std::vector<std::string> command;
    command.push_back(selfPath.string());
    command.insert(command.end(), args.begin(), args.end());
    return command;
  }

  static void test_successful_build_task()
  {
    BuildTask task = command_task("success", self_command({"--child-output", "success"}));
    const BuildTaskResult result = execute_build_task_process(task);

    require(result.taskId == "success", "success task id");
    require(result.state == BuildTaskState::Done, "success state");
    require(result.exitCode == 0, "success exit code");
    require(result.output == "success", "success output");
  }

  static void test_failed_build_task()
  {
    BuildTask task = command_task("fail", self_command({"--child-exit", "7"}));
    const BuildTaskResult result = execute_build_task_process(task);

    require(result.taskId == "fail", "failure task id");
    require(result.state == BuildTaskState::Failed, "failure state");
    require(result.exitCode == 7, "failure exit");
  }

  static void test_output_propagation()
  {
    BuildTask task = command_task("capture", self_command({"--child-mixed-output"}));
    const BuildTaskResult result = execute_build_task_process(task);

    require(result.state == BuildTaskState::Done, "capture state");
    require(result.output == "out\nerr\n", "merged output propagated");
  }

  static void test_working_directory_propagation()
  {
    TempDir temp;
    BuildTask task = command_task("pwd", self_command({"--child-pwd"}));
    task.workingDirectory = temp.path;

    const BuildTaskResult result = execute_build_task_process(task);

    require(result.state == BuildTaskState::Done, "working directory state");
    require(result.output.find(temp.path.string()) != std::string::npos, "working directory propagated");
  }

  static void test_empty_command()
  {
    BuildTask task = command_task("empty", {});
    const BuildTaskResult result = execute_build_task_process(task);

    require(result.taskId == "empty", "empty task id");
    require(result.state == BuildTaskState::Failed, "empty state");
    require(result.exitCode == 127, "empty exit");
    require(result.output.find("Empty build command") != std::string::npos, "empty output");
  }

  static int child_main(int argc, char **argv)
  {
    const std::string mode = argc > 1 ? argv[1] : "";

    if (mode == "--child-output")
    {
      if (argc > 2)
        std::cout << argv[2];
      return 0;
    }

    if (mode == "--child-exit")
      return argc > 2 ? std::atoi(argv[2]) : 0;

    if (mode == "--child-mixed-output")
    {
      std::cout << "out\n"
                << std::flush;
      std::cerr << "err\n"
                << std::flush;
      return 0;
    }

    if (mode == "--child-pwd")
    {
      std::cout << fs::current_path().string();
      return 0;
    }

    return 2;
  }
} // namespace

int main(int argc, char **argv)
{
  if (argc > 1 && std::string(argv[1]).rfind("--child-", 0) == 0)
    return child_main(argc, argv);

  try
  {
    selfPath = fs::absolute(argv[0]).lexically_normal();

    test_successful_build_task();
    test_failed_build_task();
    test_output_propagation();
    test_working_directory_propagation();
    test_empty_command();
  }
  catch (const std::exception &ex)
  {
    std::cerr << "BuildTaskProcessExecutorTests failed: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
