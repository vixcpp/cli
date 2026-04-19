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

#include <nlohmann/json.hpp>

#include <cstdio>

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#else
#include <io.h>
#include <fcntl.h>
#endif

using namespace vix::cli::style;
namespace fs = std::filesystem;

namespace
{
  std::atomic<bool> g_stop{false};

  void on_sigint(int)
  {
    g_stop.store(true);
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

  static bool has_flag(
      const std::vector<std::string> &args,
      const std::string &flag)
  {
    return std::find(args.begin(), args.end(), flag) != args.end();
  }

  static std::string resolve_preset_name(const vix::commands::TestsCommand::detail::Options &opt)
  {
    if (auto p = value_after_flag(opt.forwarded, "--preset"))
      return *p;

    if (auto p = value_after_flag(opt.forwarded, "-p"))
      return *p;

    return "dev-ninja";
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

  static int run_in_dir(const fs::path &cwd, const std::vector<std::string> &argv)
  {
    ScopedCwd sc(cwd);

    const std::string cmd = shell_join(argv);
    step(std::string("Exec: ") + cmd);

    const int raw = std::system(cmd.c_str());
    return vix::cli::process::normalize_exit_code(raw);
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

  static bool should_force_ctest(const vix::commands::TestsCommand::detail::Options &opt)
  {
    if (!opt.ctestArgs.empty())
      return true;

    return false;
  }

  static int run_native_tests(const vix::commands::TestsCommand::detail::Options &opt)
  {
    const std::string presetName = resolve_preset_name(opt);
    const fs::path buildDir = resolve_build_dir_from_preset(opt.projectDir, presetName);

    std::error_code ec;
    if (!fs::exists(buildDir, ec) || !fs::is_directory(buildDir, ec))
    {
      return 2;
    }

    const auto runner = find_native_test_runner(buildDir);

    if (!runner)
      return 2;

    std::vector<std::string> argv;
    argv.push_back(runner->string());

    return run_in_dir(buildDir, argv);
  }

  static int run_ctest(const vix::commands::TestsCommand::detail::Options &opt)
  {
    const std::string presetName = resolve_preset_name(opt);
    const fs::path buildDir = resolve_build_dir_from_preset(opt.projectDir, presetName);

    std::error_code ec;
    if (!fs::exists(buildDir, ec) || !fs::is_directory(buildDir, ec))
    {
      error("No build directory found for tests.");
      hint("Run vix build with tests enabled first.");
      return 1;
    }

    if (!ctest_file_exists(buildDir))
    {
      error("No tests available.");
      hint("Tests were not generated in the existing build directory.");
      return 1;
    }

    std::vector<std::string> argv;
    argv.push_back("ctest");

    for (const auto &a : opt.ctestArgs)
      argv.push_back(a);

    return run_in_dir(buildDir, argv);
  }

  static int run_tests_once(const vix::commands::TestsCommand::detail::Options &opt)
  {
    if (should_force_ctest(opt))
      return run_ctest(opt);

    const int nativeCode = run_native_tests(opt);
    if (nativeCode == 0)
      return 0;

    if (nativeCode != 2)
      return nativeCode;

    return run_ctest(opt);
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
    out << "  vix tests [path] [options]\n\n";

    out << "Description:\n";
    out << "  Run project tests.\n";
    out << "  Native test runner is preferred.\n";
    out << "  CTest is used as a fallback or when raw CTest args are passed.\n";
    out << "  Build directory is resolved from CMakePresets.json (binaryDir).\n\n";

    out << "Tests flags:\n";
    out << "  --watch                   Watch files and re-run tests on changes\n";
    out << "  --run                     Run runtime check after tests (tests + runtime)\n";
    out << "  --list                    Forward to CTest (--show-only)\n";
    out << "  --fail-fast               Forward to CTest (--stop-on-failure)\n\n";

    out << "CTest passthrough:\n";
    out << "  Use `--` to pass raw arguments to ctest.\n";
    out << "  Passing raw CTest args forces CTest mode.\n";
    out << "  Example: vix tests -- --output-on-failure -R MySuite\n\n";

    out << "Notes:\n";
    out << "  - Preset is taken from forwarded args (e.g. --preset release)\n";
    out << "    or defaults to dev-ninja.\n";
    out << "  - If tests are not configured yet, `vix check --tests` is used.\n";
    out << "  - All other options supported by `vix check` can still be forwarded.\n\n";

    out << "Examples:\n";
    out << "  vix tests\n";
    out << "  vix tests --watch\n";
    out << "  vix tests --run\n";
    out << "  vix tests ./examples/blog\n";
    out << "  vix tests --preset release\n";
    out << "  vix tests -- --output-on-failure -R MySuite\n\n";

    out << "See also:\n";
    out << "  vix check --tests\n";

    return 0;
  }
} // namespace vix::commands::TestsCommand
