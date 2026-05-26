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
#include <vix/cli/commands/CheckCommand.hpp>
#include <vix/cli/commands/tests/TestsDetail.hpp>
#include <vix/cli/Style.hpp>

#include <vix/cli/process/Process.hpp>
#include <vix/cli/build/BuildStyle.hpp>

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
#include <cctype>

#include <nlohmann/json.hpp>

#include <cstdio>

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#else
#include <io.h>
#include <fcntl.h>
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
  }

  struct TestExecResult
  {
    int code{0};
    std::string output;
  };

  struct CTestItem
  {
    std::string name;
    std::string status;
    std::string duration;
    bool passed{false};
  };

  static int default_test_jobs()
  {
    unsigned int hc = std::thread::hardware_concurrency();

    if (hc == 0)
      return 4;

    if (hc > 64)
      hc = 64;

    return static_cast<int>(hc);
  }

  static bool tests_verbose_enabled(
      const vix::commands::TestsCommand::detail::Options &opt)
  {
    for (const auto &arg : opt.forwarded)
    {
      if (arg == "-v" || arg == "--verbose")
        return true;
    }

    return false;
  }

  static std::string display_preset_name(const std::string &presetName)
  {
    if (presetName == "dev-ninja")
      return "dev";

    return presetName;
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

  struct ScopedSilenceStdStreams
  {
    bool active{false};

#ifndef _WIN32
    int savedStdout{-1};
    int savedStderr{-1};
    int nullFd{-1};
#else
    int savedStdout{-1};
    int savedStderr{-1};
    int nullFd{-1};
#endif

    ScopedSilenceStdStreams()
    {
      std::fflush(stdout);
      std::fflush(stderr);

#ifndef _WIN32
      nullFd = ::open("/dev/null", O_WRONLY);
      if (nullFd == -1)
        return;

      savedStdout = ::dup(STDOUT_FILENO);
      savedStderr = ::dup(STDERR_FILENO);

      if (savedStdout == -1 || savedStderr == -1)
      {
        if (savedStdout != -1)
          ::close(savedStdout);
        if (savedStderr != -1)
          ::close(savedStderr);
        ::close(nullFd);
        savedStdout = -1;
        savedStderr = -1;
        nullFd = -1;
        return;
      }

      if (::dup2(nullFd, STDOUT_FILENO) == -1 ||
          ::dup2(nullFd, STDERR_FILENO) == -1)
      {
        ::dup2(savedStdout, STDOUT_FILENO);
        ::dup2(savedStderr, STDERR_FILENO);
        ::close(savedStdout);
        ::close(savedStderr);
        ::close(nullFd);
        savedStdout = -1;
        savedStderr = -1;
        nullFd = -1;
        return;
      }

      active = true;
#else
      nullFd = _open("NUL", _O_WRONLY);
      if (nullFd == -1)
        return;

      savedStdout = _dup(_fileno(stdout));
      savedStderr = _dup(_fileno(stderr));

      if (savedStdout == -1 || savedStderr == -1)
      {
        if (savedStdout != -1)
          _close(savedStdout);
        if (savedStderr != -1)
          _close(savedStderr);
        _close(nullFd);
        savedStdout = -1;
        savedStderr = -1;
        nullFd = -1;
        return;
      }

      if (_dup2(nullFd, _fileno(stdout)) == -1 ||
          _dup2(nullFd, _fileno(stderr)) == -1)
      {
        _dup2(savedStdout, _fileno(stdout));
        _dup2(savedStderr, _fileno(stderr));
        _close(savedStdout);
        _close(savedStderr);
        _close(nullFd);
        savedStdout = -1;
        savedStderr = -1;
        nullFd = -1;
        return;
      }

      active = true;
#endif
    }

    ~ScopedSilenceStdStreams()
    {
      if (!active)
        return;

      std::fflush(stdout);
      std::fflush(stderr);

#ifndef _WIN32
      ::dup2(savedStdout, STDOUT_FILENO);
      ::dup2(savedStderr, STDERR_FILENO);
      ::close(savedStdout);
      ::close(savedStderr);
      ::close(nullFd);
#else
      _dup2(savedStdout, _fileno(stdout));
      _dup2(savedStderr, _fileno(stderr));
      _close(savedStdout);
      _close(savedStderr);
      _close(nullFd);
#endif
    }
  };

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

  static std::string resolve_test_target_name(
      const vix::commands::TestsCommand::detail::Options &opt)
  {
    if (auto target = value_after_flag(opt.forwarded, "--build-target"))
      return *target;

    if (!opt.testPattern.empty())
      return opt.testPattern;

    return "all";
  }

  static void print_test_header(
      const vix::commands::TestsCommand::detail::Options &opt)
  {
    const std::string presetName = resolve_preset_name(opt);
    const std::string targetName = resolve_test_target_name(opt);

    std::vector<std::pair<std::string, std::string>> meta;
    meta.emplace_back("engine", "vix");
    meta.emplace_back("jobs", std::to_string(default_test_jobs()));

    build::print_task_header_full(
        std::cout,
        "Testing",
        targetName,
        display_preset_name(presetName),
        meta);
  }

  static void print_tests_separator()
  {
    std::cout << "  "
              << GRAY
              << "─────────────────────────────────────"
              << RESET
              << "\n";
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

  static std::optional<int> extract_ctest_total_count(const std::string &output)
  {
    const std::string marker = " tests failed out of ";

    const auto markerPos = output.find(marker);
    if (markerPos == std::string::npos)
      return std::nullopt;

    std::size_t begin = markerPos + marker.size();
    std::size_t end = begin;

    while (end < output.size() &&
           output[end] >= '0' &&
           output[end] <= '9')
    {
      ++end;
    }

    if (end == begin)
      return std::nullopt;

    try
    {
      return std::stoi(output.substr(begin, end - begin));
    }
    catch (...)
    {
      return std::nullopt;
    }
  }

  static std::optional<int> extract_ctest_failed_count(const std::string &output)
  {
    const std::string marker = " tests failed out of ";

    const auto markerPos = output.find(marker);
    if (markerPos == std::string::npos)
      return std::nullopt;

    std::size_t end = markerPos;
    std::size_t begin = end;

    while (begin > 0 &&
           output[begin - 1] >= '0' &&
           output[begin - 1] <= '9')
    {
      --begin;
    }

    if (begin == end)
      return std::nullopt;

    try
    {
      return std::stoi(output.substr(begin, end - begin));
    }
    catch (...)
    {
      return std::nullopt;
    }
  }

  static std::string passed_tests_message(const TestExecResult &result)
  {
    if (auto total = extract_ctest_total_count(result.output))
      return "Passed " + std::to_string(*total) + " tests";

    return "Passed tests";
  }

  static std::string failed_tests_message(const TestExecResult &result)
  {
    const auto failed = extract_ctest_failed_count(result.output);
    const auto total = extract_ctest_total_count(result.output);

    if (failed && total)
    {
      return "Failed " + std::to_string(*failed) +
             " of " + std::to_string(*total) + " tests";
    }

    return "Tests failed";
  }

  static fs::path resolve_build_dir_from_preset(
      const fs::path &projectDir,
      const std::string &presetName)
  {
    std::string dirName = "build";

    if (presetName == "dev")
      dirName = "build-dev";
    else if (presetName == "dev-ninja")
      dirName = "build-ninja";
    else if (presetName == "release")
      dirName = "build-release";

    std::error_code ec;
    return fs::weakly_canonical(projectDir / dirName, ec);
  }

  struct ScopedCwd
  {
    fs::path prev;
    bool changed{false};

    explicit ScopedCwd(const fs::path &p)
    {
      std::error_code ec;
      prev = fs::current_path(ec);
      if (!ec)
      {
        fs::current_path(p, ec);
        changed = !ec;
      }
    }

    ~ScopedCwd()
    {
      if (!changed)
        return;

      std::error_code ec;
      fs::current_path(prev, ec);
    }
  };

  static std::string shell_join(const std::vector<std::string> &argv)
  {
    std::ostringstream oss;

    for (std::size_t i = 0; i < argv.size(); ++i)
    {
      if (i)
        oss << ' ';

      const std::string &a = argv[i];
      const bool needQuotes =
          (a.find(' ') != std::string::npos) ||
          (a.find('\t') != std::string::npos);

      if (!needQuotes)
      {
        oss << a;
      }
      else
      {
        oss << '"';
        for (char c : a)
        {
          if (c == '"')
            oss << "\\\"";
          else
            oss << c;
        }
        oss << '"';
      }
    }

    return oss.str();
  }

  static TestExecResult run_in_dir_capture(
      const fs::path &cwd,
      const std::vector<std::string> &argv)
  {
    TestExecResult result;

    ScopedCwd sc(cwd);

    const std::string cmd = shell_join(argv) + " 2>&1";

    FILE *pipe = VIX_TESTS_POPEN(cmd.c_str(), "r");
    if (!pipe)
    {
      result.code = 127;
      result.output = "failed to start test process";
      return result;
    }

    char buffer[4096];

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
      result.output += buffer;

    const int raw = VIX_TESTS_PCLOSE(pipe);
    result.code = vix::cli::process::normalize_exit_code(raw);

    return result;
  }

  static bool file_is_executable(const fs::path &p)
  {
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec))
      return false;

#ifdef _WIN32
    return true;
#else
    auto perms = fs::status(p, ec).permissions();
    if (ec)
      return false;

    using perms_t = fs::perms;
    return ((perms & perms_t::owner_exec) != perms_t::none) ||
           ((perms & perms_t::group_exec) != perms_t::none) ||
           ((perms & perms_t::others_exec) != perms_t::none);
#endif
  }

  static fs::path append_exe_if_needed(const fs::path &p)
  {
#ifdef _WIN32
    if (p.extension() == ".exe")
      return p;
    return p.string() + ".exe";
#else
    return p;
#endif
  }

  static bool looks_like_test_binary_name(const std::string &name)
  {
    if (name.empty())
      return false;

    if (name == "vix_umbrella_tests" ||
        name == "vix_tests_runner" ||
        name == "vix_tests" ||
        name == "tests" ||
        name == "test")
    {
      return true;
    }

    if (name.size() >= 5 && name.rfind("_test") == name.size() - 5)
      return true;

    if (name.size() >= 6 && name.rfind("_tests") == name.size() - 6)
      return true;

    if (name.size() >= 11 && name.rfind("_basic_test") == name.size() - 11)
      return true;

    return false;
  }

  static std::vector<fs::path> native_test_candidates(const fs::path &buildDir)
  {
    std::vector<fs::path> candidates;

    const std::vector<std::string> names = {
        "vix_umbrella_tests",
        "vix_tests_runner",
        "vix_tests",
        "tests",
        "test",
    };

    const std::vector<fs::path> roots = {
        buildDir,
        buildDir / "bin",
        buildDir / "tests",
        buildDir / "test",
        buildDir / "Debug",
        buildDir / "Release",
        buildDir / "RelWithDebInfo",
        buildDir / "MinSizeRel",
    };

    for (const auto &root : roots)
    {
      for (const auto &name : names)
        candidates.push_back(append_exe_if_needed(root / name));
    }

    return candidates;
  }

  static std::optional<fs::path> find_native_test_runner(const fs::path &buildDir)
  {
    // 1. Known fixed candidates first
    for (const auto &candidate : native_test_candidates(buildDir))
    {
      if (file_is_executable(candidate))
        return candidate;
    }

    // 2. Then scan common build roots for project-generated test binaries
    const std::vector<fs::path> roots = {
        buildDir,
        buildDir / "bin",
        buildDir / "tests",
        buildDir / "test",
        buildDir / "Debug",
        buildDir / "Release",
        buildDir / "RelWithDebInfo",
        buildDir / "MinSizeRel",
    };

    for (const auto &root : roots)
    {
      std::error_code ec;
      if (!fs::exists(root, ec) || !fs::is_directory(root, ec))
        continue;

      for (fs::directory_iterator it(root, ec), end; it != end; it.increment(ec))
      {
        if (ec)
          continue;

        const fs::path p = it->path();

        if (!it->is_regular_file(ec))
          continue;

        const std::string stem = p.stem().string();
        const std::string filename = p.filename().string();

#ifdef _WIN32
        const bool exe_ok = p.extension() == ".exe";
#else
        const bool exe_ok = true;
#endif

        if (!exe_ok)
          continue;

        if (!looks_like_test_binary_name(stem) &&
            !looks_like_test_binary_name(filename))
        {
          continue;
        }

        if (file_is_executable(p))
          return p;
      }
    }

    return std::nullopt;
  }

  static bool ctest_file_exists(const fs::path &buildDir)
  {
    std::error_code ec;
    return fs::exists(buildDir / "CTestTestfile.cmake", ec) && !ec;
  }

  static bool has_test_sources(const fs::path &projectDir)
  {
    const fs::path testsDir = projectDir / "tests";

    std::error_code ec;

    if (!fs::exists(testsDir, ec) || ec)
      return false;

    if (!fs::is_directory(testsDir, ec) || ec)
      return false;

    for (fs::recursive_directory_iterator it(
             testsDir,
             fs::directory_options::skip_permission_denied,
             ec),
         end;
         it != end;
         it.increment(ec))
    {
      if (ec)
        continue;

      if (!it->is_regular_file(ec))
        continue;

      const std::string ext = it->path().extension().string();

      if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c")
        return true;
    }

    return false;
  }

  static std::string sanitize_cmake_var_component(std::string value)
  {
    for (char &c : value)
    {
      const unsigned char ch = static_cast<unsigned char>(c);

      if (!std::isalnum(ch) && c != '_')
        c = '_';
    }

    if (value.empty())
      return "PROJECT";

    return value;
  }

  static std::string project_tests_option_name(const fs::path &projectDir)
  {
    return sanitize_cmake_var_component(projectDir.filename().string()) +
           "_BUILD_TESTS";
  }

  static int build_project_tests(
      const vix::commands::TestsCommand::detail::Options &opt)
  {
    const std::string presetName = resolve_preset_name(opt);
    const std::string testsOption = project_tests_option_name(opt.projectDir);

    std::vector<std::string> argv;

    argv.push_back("vix");
    argv.push_back("build");
    argv.push_back("--build-target");
    argv.push_back("all");

    if (!presetName.empty())
    {
      argv.push_back("--preset");
      argv.push_back(presetName);
    }

    argv.push_back("--");
    argv.push_back("-DBUILD_TESTING=ON");
    argv.push_back("-D" + testsOption + "=ON");

    build::print_task_header_full(
        std::cout,
        "Preparing tests",
        "all",
        display_preset_name(presetName),
        {});

    const auto start = std::chrono::steady_clock::now();
    const TestExecResult result = run_in_dir_capture(opt.projectDir, argv);
    const auto end = std::chrono::steady_clock::now();

    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();

    if (result.code == 0)
    {
      build::print_task_success_timed(
          std::cout,
          "Tests configured",
          ms);

      return 0;
    }

    build::print_task_failure_timed(
        std::cout,
        "Failed to configure tests",
        ms);

    if (!result.output.empty())
      std::cout << "\n"
                << result.output << "\n";

    return result.code == 0 ? 1 : result.code;
  }

  static bool should_force_ctest(const vix::commands::TestsCommand::detail::Options &opt)
  {
    if (opt.list)
      return true;

    if (opt.raw)
      return true;

    if (!opt.ctestArgs.empty())
      return true;

    return false;
  }

  struct ParsedTestFailure
  {
    std::string name;
    std::string message;
  };

  static std::string trim_copy(std::string value)
  {
    while (!value.empty() &&
           (value.back() == ' ' ||
            value.back() == '\t' ||
            value.back() == '\n' ||
            value.back() == '\r'))
    {
      value.pop_back();
    }

    std::size_t i = 0;
    while (i < value.size() &&
           (value[i] == ' ' ||
            value[i] == '\t' ||
            value[i] == '\n' ||
            value[i] == '\r'))
    {
      ++i;
    }

    value.erase(0, i);
    return value;
  }

  static std::string extract_ctest_duration(const std::string &line)
  {
    const std::string marker = " sec";
    const auto secPos = line.rfind(marker);

    if (secPos == std::string::npos)
      return {};

    std::size_t end = secPos;
    while (end > 0 && std::isspace(static_cast<unsigned char>(line[end - 1])))
      --end;

    std::size_t begin = end;
    while (begin > 0 && !std::isspace(static_cast<unsigned char>(line[begin - 1])))
      --begin;

    if (begin >= end)
      return {};

    return line.substr(begin, end - begin) + "s";
  }

  static bool is_ctest_failure_line(const std::string &line)
  {
    return line.find("***Failed") != std::string::npos ||
           line.find("***Exception") != std::string::npos ||
           line.find("***Timeout") != std::string::npos ||
           line.find("***Not Run") != std::string::npos ||
           line.find("Subprocess aborted") != std::string::npos ||
           line.find("SEGFAULT") != std::string::npos ||
           line.find("SegFault") != std::string::npos;
  }

  static std::string extract_ctest_failure_reason(const std::string &line)
  {
    const std::vector<std::string> markers = {
        "Subprocess aborted",
        "***Failed",
        "***Exception",
        "***Timeout",
        "***Not Run",
        "SEGFAULT",
        "SegFault",
    };

    std::size_t pos = std::string::npos;

    for (const std::string &marker : markers)
    {
      const std::size_t p = line.find(marker);
      if (p != std::string::npos && (pos == std::string::npos || p < pos))
        pos = p;
    }

    if (pos == std::string::npos)
      return {};

    return trim_copy(line.substr(pos));
  }

  static std::vector<CTestItem> parse_ctest_run_output(
      const std::string &output)
  {
    std::vector<CTestItem> tests;

    std::istringstream in(output);
    std::string line;

    while (std::getline(in, line))
    {
      const std::string trimmed = trim_copy(line);

      const std::string marker = "Test #";
      const std::size_t markerPos = trimmed.find(marker);

      if (markerPos == std::string::npos)
        continue;

      const std::size_t colon = trimmed.find(':', markerPos);
      if (colon == std::string::npos)
        continue;

      std::string rest = trim_copy(trimmed.substr(colon + 1));

      const bool passed = rest.find(" Passed") != std::string::npos;
      const bool failed = is_ctest_failure_line(rest);
      const bool skipped = rest.find("Skipped") != std::string::npos ||
                           rest.find("Not Run") != std::string::npos;

      if (!passed && !failed && !skipped)
        continue;

      std::size_t nameEnd = rest.find("...");
      if (nameEnd == std::string::npos)
      {
        nameEnd = rest.find(" Passed");
        if (nameEnd == std::string::npos)
          nameEnd = rest.find("***Failed");
        if (nameEnd == std::string::npos)
          nameEnd = rest.find("Skipped");
        if (nameEnd == std::string::npos)
          nameEnd = rest.find("Not Run");
      }

      CTestItem item;
      item.name = trim_copy(rest.substr(0, nameEnd));
      item.passed = passed;
      item.status = passed ? "passed" : (failed ? "failed" : "skipped");
      item.duration = extract_ctest_duration(trimmed);

      if (!item.name.empty())
        tests.push_back(item);
    }

    return tests;
  }

  static void print_ctest_items(
      const std::vector<CTestItem> &tests)
  {
    if (tests.empty())
      return;

    print_tests_separator();

    for (const CTestItem &test : tests)
    {
      const char *statusColor = test.passed ? GREEN : RED;
      const char *mark = test.passed ? "✓" : "✖";

      std::cout << "  "
                << statusColor << mark << RESET
                << " "
                << statusColor << BOLD << "unit" << RESET
                << " "
                << test.name;

      if (!test.duration.empty())
        std::cout << " " << GRAY << test.duration << RESET;

      std::cout << "\n";
    }

    std::cout << "\n";
  }

  static std::vector<std::string> parse_ctest_list_output(
      const std::string &output)
  {
    std::vector<std::string> tests;

    std::istringstream in(output);
    std::string line;

    while (std::getline(in, line))
    {
      const std::string trimmed = trim_copy(line);

      const std::string marker = "Test #";
      const std::size_t markerPos = trimmed.find(marker);

      if (markerPos == std::string::npos)
        continue;

      const std::size_t colon = trimmed.find(':', markerPos);
      if (colon == std::string::npos)
        continue;

      std::string name = trim_copy(trimmed.substr(colon + 1));

      if (!name.empty())
        tests.push_back(name);
    }

    return tests;
  }

  static void print_listed_tests(
      const std::vector<std::string> &tests)
  {
    print_tests_separator();

    if (tests.empty())
    {
      hint("No tests matched the current filter.");
      return;
    }

    std::cout << "  " << CYAN << "tests" << RESET << "\n";

    for (const std::string &test : tests)
    {
      std::cout << "    "
                << GRAY << "• " << RESET
                << test
                << "\n";
    }

    std::cout << "\n";
  }

  static bool starts_with_test_result_line(const std::string &line)
  {
    const auto slash = line.find('/');
    if (slash == std::string::npos)
      return false;

    if (slash == 0)
      return false;

    for (std::size_t i = 0; i < slash; ++i)
    {
      if (!std::isdigit(static_cast<unsigned char>(line[i])))
        return false;
    }

    return line.find(" Test ") != std::string::npos;
  }

  static std::optional<std::string> extract_failed_test_name_from_line(
      const std::string &line)
  {
    if (!is_ctest_failure_line(line))
      return std::nullopt;

    const auto colon = line.find(':');
    if (colon == std::string::npos)
      return std::nullopt;

    std::string rest = trim_copy(line.substr(colon + 1));

    const auto dots = rest.find("...");
    if (dots != std::string::npos)
      rest = rest.substr(0, dots);

    const std::vector<std::string> markers = {
        "Subprocess aborted",
        "***Failed",
        "***Exception",
        "***Timeout",
        "***Not Run",
        "SEGFAULT",
        "SegFault",
    };

    for (const std::string &marker : markers)
    {
      const auto markerPos = rest.find(marker);
      if (markerPos != std::string::npos)
      {
        rest = rest.substr(0, markerPos);
        break;
      }
    }

    rest = trim_copy(rest);

    if (rest.empty())
      return std::nullopt;

    return rest;
  }

  static std::vector<ParsedTestFailure> parse_test_failures(
      const std::string &output)
  {
    std::vector<ParsedTestFailure> failures;

    std::istringstream in(output);
    std::string line;

    ParsedTestFailure current;
    bool collecting = false;

    auto flush = [&]()
    {
      if (current.name.empty())
        return;

      current.message = trim_copy(current.message);

      failures.push_back(current);
      current = ParsedTestFailure{};
    };

    while (std::getline(in, line))
    {
      const std::string trimmed = trim_copy(line);

      if (auto name = extract_failed_test_name_from_line(line))
      {
        flush();

        current.name = *name;

        const std::string reason = extract_ctest_failure_reason(line);
        if (!reason.empty())
          current.message = reason;

        collecting = true;
        continue;
      }

      if (!collecting)
        continue;

      if (trimmed.empty())
        continue;

      if (trimmed.rfind("Start ", 0) == 0)
        continue;

      if (starts_with_test_result_line(trimmed))
      {
        flush();
        collecting = false;

        if (auto name = extract_failed_test_name_from_line(trimmed))
        {
          current.name = *name;

          const std::string reason = extract_ctest_failure_reason(trimmed);
          if (!reason.empty())
            current.message = reason;

          collecting = true;
        }

        continue;
      }

      if (trimmed.find("% tests passed") != std::string::npos ||
          trimmed.find("tests failed out of") != std::string::npos ||
          trimmed.find("Total Test time") != std::string::npos ||
          trimmed.find("The following tests FAILED") != std::string::npos ||
          trimmed.find("Errors while running CTest") != std::string::npos ||
          trimmed.rfind("Test project ", 0) == 0)
      {
        continue;
      }

      if (!current.message.empty())
        current.message += "\n";

      current.message += trimmed;
    }

    flush();

    return failures;
  }

  static void print_clean_test_failure_details(
      const TestExecResult &result,
      bool verbose)
  {
    const auto failures = parse_test_failures(result.output);

    if (!failures.empty())
    {
      std::cout << "\n";
      std::cout << "  " << CYAN << "failed:" << RESET << "\n";

      for (const auto &failure : failures)
        std::cout << "    " << failure.name << "\n";

      const auto &first = failures.front();

      if (!first.message.empty())
      {
        std::cout << "\n";
        std::cout << "  " << CYAN << "error:" << RESET << "\n";
        std::cout << "    " << first.message << "\n";
      }
    }
    else
    {
      std::cout << "\n";
      error("Tests failed");
    }

    std::cout << "\n";

    if (verbose)
    {
      if (!failures.empty())
      {
        std::cout << "  " << CYAN << "details:" << RESET << "\n";

        for (const auto &failure : failures)
        {
          std::cout << "    " << failure.name << "\n";

          if (!failure.message.empty())
          {
            std::istringstream lines(failure.message);
            std::string line;

            while (std::getline(lines, line))
              std::cout << "      " << line << "\n";
          }
        }

        return;
      }

      hint("No structured failure details were found.");
      return;
    }

    hint("Run `vix tests -v` to show detailed Vix test output.");
    hint("Run `vix tests --raw` to show raw runner output.");
  }

  static int run_native_tests(const vix::commands::TestsCommand::detail::Options &opt)
  {
    const std::string presetName = resolve_preset_name(opt);
    const fs::path buildDir = resolve_build_dir_from_preset(opt.projectDir, presetName);

    std::error_code ec;
    if (!fs::exists(buildDir, ec) || !fs::is_directory(buildDir, ec))
      return 2;

    const auto runner = find_native_test_runner(buildDir);

    if (!runner)
      return 2;

    std::vector<std::string> argv;
    argv.push_back(runner->string());

    print_test_header(opt);

    const auto start = std::chrono::steady_clock::now();
    const TestExecResult result = run_in_dir_capture(buildDir, argv);
    const auto end = std::chrono::steady_clock::now();

    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    const bool ok = result.code == 0;

    if (ok)
    {
      build::print_task_success_timed(std::cout, "Passed tests", ms);
      return 0;
    }

    build::print_task_failure_timed(std::cout, "Tests failed", ms);

    if (opt.raw)
    {
      std::cout << "\n";
      std::cout << result.output;
    }
    else
    {
      print_clean_test_failure_details(
          result,
          tests_verbose_enabled(opt));
    }

    return result.code;
  }

  static int run_ctest(const vix::commands::TestsCommand::detail::Options &opt)
  {
    const std::string presetName = resolve_preset_name(opt);
    const fs::path buildDir = resolve_build_dir_from_preset(opt.projectDir, presetName);

    std::error_code ec;
    if (!fs::exists(buildDir, ec) || !fs::is_directory(buildDir, ec))
    {
      error("No build directory found for tests.");
      hint("Run `vix build --build-target all` first.");
      return 1;
    }

    if (!ctest_file_exists(buildDir))
      return 2;

    std::vector<std::string> argv;
    argv.push_back("ctest");

    if (opt.list)
      argv.push_back("-N");
    else
      argv.push_back("--output-on-failure");

    if (!has_ctest_parallel_arg(opt.ctestArgs))
    {
      argv.push_back("--parallel");
      argv.push_back(std::to_string(default_test_jobs()));
    }

    for (const auto &a : opt.ctestArgs)
      argv.push_back(a);

    const auto start = std::chrono::steady_clock::now();
    const TestExecResult result = run_in_dir_capture(buildDir, argv);
    const auto end = std::chrono::steady_clock::now();

    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    const bool ok = result.code == 0;

    if (ok && opt.list)
    {
      const std::vector<std::string> tests =
          parse_ctest_list_output(result.output);

      if (tests.empty() && has_test_sources(opt.projectDir))
        return 2;

      print_test_header(opt);
      print_listed_tests(tests);

      build::print_task_success_timed(
          std::cout,
          "Listed " +
              std::to_string(tests.size()) +
              " test" +
              (tests.size() == 1 ? "" : "s"),
          ms);

      return 0;
    }

    print_test_header(opt);

    if (ok)
    {
      const std::vector<CTestItem> tests =
          parse_ctest_run_output(result.output);

      if (tests.empty() &&
          has_test_sources(opt.projectDir) &&
          result.output.find("No tests were found") != std::string::npos)
      {
        return 2;
      }

      if (opt.raw)
      {
        std::cout << "\n";
        std::cout << result.output;
      }
      else
      {
        print_ctest_items(tests);
      }

      build::print_task_success_timed(
          std::cout,
          passed_tests_message(result),
          ms);

      if (tests_verbose_enabled(opt))
      {
        std::cout << "\n";
        std::cout << result.output;
      }

      return 0;
    }

    build::print_task_failure_timed(
        std::cout,
        failed_tests_message(result),
        ms);

    if (opt.raw)
    {
      std::cout << "\n";
      std::cout << result.output;
    }
    else
    {
      print_clean_test_failure_details(
          result,
          tests_verbose_enabled(opt));
    }

    return result.code;
  }
  static int run_tests_once(const vix::commands::TestsCommand::detail::Options &opt)
  {
    auto run_available_tests = [&]() -> int
    {
      const std::string presetName = resolve_preset_name(opt);
      const fs::path buildDir =
          resolve_build_dir_from_preset(opt.projectDir, presetName);

      if (ctest_file_exists(buildDir))
        return run_ctest(opt);

      const int nativeCode = run_native_tests(opt);

      if (nativeCode == 0)
        return 0;

      if (nativeCode != 2)
        return nativeCode;

      return run_ctest(opt);
    };

    int code = run_available_tests();

    if (code != 2)
      return code;

    if (!has_test_sources(opt.projectDir))
    {
      error("No tests available.");
      hint("No test source files were found in the tests directory.");
      return 1;
    }

    const int buildCode = build_project_tests(opt);

    if (buildCode != 0)
      return buildCode;

    code = run_available_tests();

    if (code != 2)
      return code;

    error("No tests available.");
    hint("Tests were found, but no runnable test target was generated.");
    hint("Check that your CMakeLists.txt enables tests when " +
         project_tests_option_name(opt.projectDir) + "=ON.");

    return 1;
  }
} // namespace

namespace vix::commands::TestsCommand
{
  int run(const std::vector<std::string> &args)
  {
    const auto opt = vix::commands::TestsCommand::detail::parse(args);

    if (!opt.watch)
    {
      const int code = run_tests_once(opt);

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

    if (should_force_ctest(opt))
      hint("Mode: CTest forced by passthrough args.");
    else
      hint("Mode: native runner first, CTest fallback.");

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
    out << "  vix tests [path] [options] [-- ctest args...]\n\n";

    out << "Description:\n";
    out << "  Build, discover, and run project tests.\n";
    out << "  Vix first tries a native test runner, then falls back to CTest.\n";
    out << "  If test sources exist but tests are not configured yet, Vix can prepare\n";
    out << "  the test build automatically before running them.\n\n";

    out << "Project:\n";
    out << "  [path]                    Project directory, default: current directory\n\n";

    out << "Test options:\n";
    out << "  --watch                   Watch files and re-run tests on changes\n";
    out << "  --list                    List discovered tests without running them\n";
    out << "  --test <name|regex>       Run tests matching a name or regex\n";
    out << "  --test=<name|regex>       Same as --test <name|regex>\n";
    out << "  -R <name|regex>           Alias for --test <name|regex>\n";
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

    out << "Examples:\n";
    out << "  vix tests\n";
    out << "  vix tests --list\n";
    out << "  vix tests --watch\n";
    out << "  vix tests --run\n";
    out << "  vix tests ./examples/blog\n";
    out << "  vix tests --preset release\n";
    out << "  vix tests --test tree.basic\n";
    out << "  vix tests --test=tree.basic\n";
    out << "  vix tests -R tree.basic\n";
    out << "  vix tests --fail-fast\n";
    out << "  vix tests --raw\n";
    out << "  vix tests -- --output-on-failure\n";
    out << "  vix tests -- --output-on-failure -R MySuite\n\n";

    out << "Behavior:\n";
    out << "  native runner             Preferred when a test executable is found\n";
    out << "  CTest                     Used as fallback or when CTest args are passed\n";
    out << "  tests/ directory          Used to detect whether tests should be prepared\n";
    out << "  --list                    Shows clean test names instead of raw CTest paths\n\n";

    out << "See also:\n";
    out << "  vix build --build-target all\n";
    out << "  vix dev\n";
    out << "  vix check --tests\n";

    return 0;
  }
} // namespace vix::commands::TestsCommand
