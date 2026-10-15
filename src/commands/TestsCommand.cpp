/**
 *
 *  @file TestsCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/TestsCommand.hpp>
#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/cli/commands/CheckCommand.hpp>
#include <vix/cli/commands/tests/TestsDetail.hpp>
#include <vix/cli/Style.hpp>

#include <vix/cli/build/BuildContext.hpp>
#include <vix/cli/project/ProjectDiscovery.hpp>
#include <vix/cli/project/NativeProjectCommandDispatch.hpp>
#include <vix/cli/project/TestSelection.hpp>
#include <vix/cli/project/cmake/CMakeProjectAdapter.hpp>
#include <vix/cli/project/cmake/CTestExecution.hpp>
#include <vix/cli/project/native/NativeDevelopmentRuntime.hpp>
#include <vix/engine/ExecutionPlanning.hpp>
#include <vix/engine/Process.hpp>

#include <vix/cli/process/Process.hpp>
#include <vix/cli/build/BuildStyle.hpp>
#include <vix/cli/errors/build/BuildErrorDetectors.hpp>

#include <filesystem>
#include <unordered_map>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <iostream>
#include <fstream>
#include <sstream>
#include <optional>
#include <cstdlib>
#include <algorithm>
#include <regex>
#include <cctype>
#include <mutex>

#include <nlohmann/json.hpp>

#include <cstdio>

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#else
#include <io.h>
#include <fcntl.h>
#endif

#ifndef _WIN32
#include <sys/ioctl.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#endif

#ifdef _WIN32
#define VIX_TESTS_POPEN _popen
#define VIX_TESTS_PCLOSE _pclose
#else
#define VIX_TESTS_POPEN popen
#define VIX_TESTS_PCLOSE pclose
#endif

using namespace vix::cli::style;
namespace fs = std::filesystem;
namespace build = vix::cli::build;

namespace
{
  std::atomic<bool> g_stop{false};

  void on_sigint(int)
  {
    g_stop.store(true);

#ifndef _WIN32
    static constexpr char clearLine[] = "\r\033[2K";
    const ssize_t written =
        ::write(STDOUT_FILENO, clearLine, sizeof(clearLine) - 1);
    (void)written;
#endif
  }

  static int default_test_jobs()
  {
    unsigned int hc = std::thread::hardware_concurrency();

    if (hc == 0)
      return 4;

    if (hc > 64)
      hc = 64;

    return static_cast<int>(hc);
  }

  static bool is_ignored_dir(const fs::path &p)
  {
    const std::string name = p.filename().string();
    if (name.empty())
      return true;

    if (name == ".git" || name == ".idea" || name == ".vscode")
      return true;

    if (name.rfind("build", 0) == 0)
      return true;

    if (name == "dist")
      return true;

    return false;
  }

  static bool is_watched_file(const fs::path &p)
  {
    const auto ext = p.extension().string();
    return ext == ".cpp" || ext == ".cc" || ext == ".cxx" ||
           ext == ".hpp" || ext == ".hh" || ext == ".hxx" ||
           ext == ".h" || ext == ".cmake" ||
           p.filename() == "CMakeLists.txt" ||
           p.filename() == "CMakePresets.json";
  }

  using StampMap = std::unordered_map<std::string, std::uintmax_t>;

  static std::uintmax_t file_stamp(const fs::path &p)
  {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec)
      return 0;

    return static_cast<std::uintmax_t>(t.time_since_epoch().count());
  }

  static StampMap snapshot_tree(const fs::path &root)
  {
    StampMap m;
    std::error_code ec;

    if (!fs::exists(root, ec))
      return m;

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;

    for (; it != end; it.increment(ec))
    {
      if (ec)
        continue;

      const fs::path p = it->path();

      if (it->is_directory(ec))
      {
        if (is_ignored_dir(p))
          it.disable_recursion_pending();
        continue;
      }

      if (!it->is_regular_file(ec))
        continue;

      if (!is_watched_file(p))
        continue;

      m[p.string()] = file_stamp(p);
    }

    return m;
  }

  static bool has_changes(const StampMap &a, const StampMap &b)
  {
    if (a.size() != b.size())
      return true;

    for (const auto &kv : a)
    {
      auto it = b.find(kv.first);
      if (it == b.end())
        return true;
      if (it->second != kv.second)
        return true;
    }

    return false;
  }

  static std::optional<std::string> value_after_flag(
      const std::vector<std::string> &args,
      const std::string &flag)
  {
    for (std::size_t i = 0; i < args.size(); ++i)
    {
      if (args[i] == flag)
      {
        if (i + 1 < args.size())
          return args[i + 1];
        return std::nullopt;
      }

      const std::string prefix = flag + "=";
      if (args[i].rfind(prefix, 0) == 0 && args[i].size() > prefix.size())
        return args[i].substr(prefix.size());
    }

    return std::nullopt;
  }

  static std::string resolve_preset_name(const vix::commands::TestsCommand::detail::Options &opt)
  {
    if (auto p = value_after_flag(opt.forwarded, "--preset"))
      return *p;

    if (auto p = value_after_flag(opt.forwarded, "-p"))
      return *p;

    return "dev-ninja";
  }

  static bool has_ctest_parallel_arg(const std::vector<std::string> &args)
  {
    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const std::string &arg = args[i];

      if (arg == "-j" ||
          arg == "--parallel" ||
          arg == "--test-load")
      {
        return true;
      }

      if (arg.rfind("-j", 0) == 0 && arg.size() > 2)
        return true;

      if (arg.rfind("--parallel=", 0) == 0)
        return true;

      if (arg.rfind("--test-load=", 0) == 0)
        return true;
    }

    return false;
  }

  static bool has_ctest_timeout_arg(const std::vector<std::string> &args)
  {
    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const std::string &arg = args[i];

      if (arg == "--timeout")
        return true;

      if (arg.rfind("--timeout=", 0) == 0)
        return true;
    }

    return false;
  }

  static bool has_native_ctest_name_filter(const std::vector<std::string> &arguments)
  {
    for (const std::string &argument : arguments)
    {
      if (argument == "-R" || argument == "--tests-regex" ||
          argument.rfind("--tests-regex=", 0) == 0)
        return true;
    }
    return false;
  }

  static int run_normalized_cmake_tests(
      const vix::commands::TestsCommand::detail::Options &opt)
  {
    const auto location = vix::cli::project::discover_project(opt.projectDir);
    if (!location.success() ||
        location.location->system != vix::cli::project::ProjectSystem::cmake)
      return -1;

    const std::string presetName = resolve_preset_name(opt);
    std::vector<std::string> buildArgs{
        "--dir", location.location->root.string(),
        "--preset", presetName,
        "--require-project-metadata",
        "--", "-DBUILD_TESTING=ON"};
    const int buildCode = vix::commands::BuildCommand::run(buildArgs);
    if (buildCode != 0)
      return buildCode;

    const auto preset = vix::cli::build::resolve_builtin_preset(presetName);
    if (!preset)
      return 1;
    vix::engine::ExecutionPlanLayoutOptions layoutOptions;
    layoutOptions.userProjectDir = location.location->root;
    layoutOptions.cmakeSourceDir = location.location->root;
    layoutOptions.preset = *preset;
    const auto layout = vix::engine::make_execution_plan_layout(layoutOptions);
    if (!layout.success())
      return 1;

    const vix::cli::project::cmake::SnapshotRequest request{
        layout.plan.buildDir, std::nullopt};
    const auto snapshot = vix::cli::project::cmake::load_cmake_project_snapshot(
        *location.location, request);
    if (!snapshot.success())
    {
      error("Unable to discover configured tests (" +
            std::string(vix::cli::project::to_string(*snapshot.error)) + ").");
      return 1;
    }

    std::optional<vix::cli::project::Test> selected;
    if (opt.exactTestName)
    {
      const auto selection = vix::cli::project::select_test_by_name(
          *snapshot.snapshot, *opt.exactTestName);
      if (!selection.success())
      {
        error("Unable to resolve test '" + *opt.exactTestName + "' (" +
              std::string(vix::cli::project::to_string(*selection.error)) + ").");
        return 1;
      }
      selected = *selection.test;
    }

    if (snapshot.snapshot->project.tests.empty())
    {
      info("No tests registered.");
      return 0;
    }

    if (opt.list && opt.testPattern.empty())
    {
      for (const auto &test : snapshot.snapshot->project.tests)
        if (!selected || test.id == selected->id)
          std::cout << test.name << "\n";
      return 0;
    }

    if (selected &&
        (!opt.testPattern.empty() || has_native_ctest_name_filter(opt.ctestArgs)))
    {
      error("--exact-test cannot be combined with a native CTest name filter.");
      return 2;
    }

    const std::optional<std::string> exactName =
        selected ? std::optional<std::string>{selected->name} : std::nullopt;
    std::vector<std::string> nativeArguments = opt.ctestArgs;
    if (!opt.testPattern.empty())
    {
      nativeArguments.push_back("-R");
      nativeArguments.push_back(opt.testPattern);
    }
    if (opt.list)
      nativeArguments.push_back("-N");
    if (!has_ctest_parallel_arg(nativeArguments))
    {
      nativeArguments.push_back("--parallel");
      nativeArguments.push_back(std::to_string(default_test_jobs()));
    }
    if (!has_ctest_timeout_arg(nativeArguments))
    {
      nativeArguments.push_back("--timeout");
      nativeArguments.push_back("60");
    }
    const auto argv = vix::cli::project::cmake::ctest_execution_argv(
        request.buildRoot, request.configuration, exactName, nativeArguments);
    vix::engine::process::Command command;
    command.argv = argv;
    command.mergeStdErr = true;
    const auto result = vix::engine::process::execute(command);

    if (!result.output.empty())
      std::cout << result.output;
    if (!result.success())
    {
      if (!result.errorMessage.empty())
        error(result.errorMessage);
      return result.exitCode == 0 ? 1 : result.exitCode;
    }
    return 0;
  }

  static int run_tests_once(const vix::commands::TestsCommand::detail::Options &opt)
  {
    const auto discovered = vix::cli::project::discover_project(opt.projectDir);
    if (!discovered.success() &&
        discovered.error == vix::cli::project::ProjectError::ambiguous_project_authority)
    {
      error("Project contains both vix.toml and CMakeLists.txt; project authority is ambiguous.");
      return 1;
    }
    const auto nativeTests = vix::cli::project::dispatch_native_tests(
      discovered, opt.exactTestName, opt.list, opt.ctestArgs, opt.testPattern,
      resolve_preset_name(opt),
      [](const vix::cli::project::ProjectLocation &location,
         const std::optional<std::string> &testName, bool listOnly,
         const std::vector<std::string> &ctestArguments, std::string_view testPattern,
         std::string_view preset)
      {
        return vix::cli::project::native::test_native_project(
          location.root, testName, listOnly, ctestArguments, testPattern, preset);
      });
    if (nativeTests)
    {
      const auto &result = *nativeTests;
      if (!result.output.empty()) std::cout << result.output;
      if (!result.success()) error("native tests failed: " + result.diagnostic);
      return result.exitCode;
    }
    const int normalized = run_normalized_cmake_tests(opt);
    if (normalized != -1)
      return normalized;

    error("No native vix.toml or external CMake project was discovered.");
    return 1;
  }
} // namespace

namespace vix::commands::TestsCommand
{
  int run(const std::vector<std::string> &args)
  {
    const auto opt = vix::commands::TestsCommand::detail::parse(args);
    g_stop.store(false);
    std::signal(SIGINT, on_sigint);

    if (!opt.watch)
    {
      const int code = run_tests_once(opt);

      if (code == 130)
        return 0;

      if (opt.runAfter)
      {
        if (code != 0)
          return code;

        info("Running runtime checks after tests (--run).");
        return vix::commands::CheckCommand::run(opt.forwarded);
      }

      return code;
    }

    info("Watching project files and re-running tests on changes...");
    hint("Press Ctrl+C to stop.");

    hint("Tests are routed through the discovered native-project or CMake project model.");

    g_stop.store(false);
    std::signal(SIGINT, on_sigint);

    const fs::path projectDir = opt.projectDir;

    StampMap prev = snapshot_tree(projectDir);
    int lastCode = run_tests_once(opt);

    const auto pollEvery = std::chrono::milliseconds(250);
    const auto debounce = std::chrono::milliseconds(450);
    auto lastChange = std::chrono::steady_clock::now();

    while (!g_stop.load())
    {
      std::this_thread::sleep_for(pollEvery);

      StampMap now = snapshot_tree(projectDir);
      if (has_changes(prev, now))
      {
        prev = std::move(now);
        lastChange = std::chrono::steady_clock::now();

        while (!g_stop.load())
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(80));

          StampMap later = snapshot_tree(projectDir);
          if (has_changes(prev, later))
          {
            prev = std::move(later);
            lastChange = std::chrono::steady_clock::now();
            continue;
          }

          const auto elapsed = std::chrono::steady_clock::now() - lastChange;
          if (elapsed >= debounce)
            break;
        }

        if (g_stop.load())
          break;

        std::cout << "\n";
        section_title(std::cout, "Tests re-run");

        lastCode = run_tests_once(opt);

        if (opt.runAfter && lastCode == 0)
        {
          info("Runtime checks after tests (--run).");
          lastCode = vix::commands::CheckCommand::run(opt.forwarded);
        }
      }
    }

    std::cout << "\n";
    success("Stopped test watch mode.");
    return lastCode;
  }

  int help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix test [path] [options] [-- ctest args...]\n\n";

    out << "Description:\n";
    out << "  Build, discover, and run project tests.\n";
    out << "  CMake projects use configured CTest metadata and CTest execution.\n";
    out << "  Native Vix projects use registered tests; CMake projects use CTest.\n";
    out << "  Native Vix projects execute registered tests; CMake projects use CTest.\n\n";

    out << "Project:\n";
    out << "  [path]                    Project directory, default: current directory\n\n";

    out << "Test options:\n";
    out << "  --watch                   Watch files and re-run tests on changes\n";
    out << "  --list                    List discovered tests without running them\n";
    out << "  --exact-test <name>       Select one exact configured CTest name\n";
    out << "  --exact-test=<name>       Same as --exact-test <name>\n";
    out << "  --test <regex>, -R <regex>  Filter CTest tests by regex\n";
    out << "  --fail-fast               Stop on first failing test\n";
    out << "  --raw                     Show raw test runner or CTest output\n\n";

    out << "Runtime check:\n";
    out << "  --run                     Run runtime checks after tests pass\n\n";

    out << "Build and preset forwarding:\n";
    out << "  --preset <name>           Use a build preset, default: dev-ninja\n";
    out << "  --preset=<name>           Same as --preset <name>\n";
    out << "  --build-target <name>     Build target used while preparing tests\n";
    out << "  --build-target=<name>     Same as --build-target <name>\n\n";

    out << "CTest passthrough:\n";
    out << "  -- [args...]              Pass raw arguments to CTest\n";
    out << "                            Passing raw CTest args forces CTest mode\n\n";
    out << "                            Use `-- -R <regex>` for advanced native filtering.\n\n";

    out << "Examples:\n";
    out << "  vix test\n";
    out << "  vix test --list\n";
    out << "  vix test --watch\n";
    out << "  vix test --run\n";
    out << "  vix test ./examples/project\n";
    out << "  vix test --preset release\n";
    out << "  vix test --exact-test tree.basic\n";
    out << "  vix test --exact-test=tree.basic\n";
    out << "  vix test -R tree.basic\n";
    out << "  vix test --fail-fast\n";
    out << "  vix test --raw\n";
    out << "  vix test -- --output-on-failure\n";
    out << "  vix test -- -R MySuite\n\n";

    out << "Behavior:\n";
    out << "  Native Vix projects       Execute registered project tests\n";
    out << "  CMake projects            Use configured CTest metadata and CTest execution\n";
    out << "  --list                    Shows clean test names instead of raw CTest paths\n\n";

    out << "See also:\n";
    out << "  vix build --build-target all\n";

    return 0;
  }
} // namespace vix::commands::TestsCommand
