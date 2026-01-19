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

#include <nlohmann/json.hpp>

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

    // ignore "build*" dirs (legacy / noise)
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

  static fs::path normalize_binary_dir(const fs::path &projectDir, const std::string &binaryDirRaw)
  {
    // CMakePresets often uses:
    //   "${sourceDir}/out/dev-ninja"
    // or relative "out/dev-ninja"
    std::string s = binaryDirRaw;

    const std::string tokenSourceDir = "${sourceDir}";
    const std::string proj = projectDir.generic_string();

    for (;;)
    {
      auto pos = s.find(tokenSourceDir);
      if (pos == std::string::npos)
        break;
      s.replace(pos, tokenSourceDir.size(), proj);
    }

    while (s.find("//") != std::string::npos)
      s.replace(s.find("//"), 2, "/");

    fs::path p = fs::path(s);

    if (p.is_relative())
      p = projectDir / p;

    return fs::weakly_canonical(p);
  }

  static fs::path resolve_build_dir_or_fallback(
      const fs::path &projectDir,
      const std::string &presetName)
  {
    const fs::path presetsPath = projectDir / "CMakePresets.json";

    std::error_code ec;
    if (!fs::exists(presetsPath, ec))
    {
      const std::vector<fs::path> candidates = {
          projectDir / "out" / presetName,
          projectDir / "out",
          projectDir / "bld" / presetName,
          projectDir / "bld",
          projectDir / ("cmake-build-" + presetName),
      };

      for (const auto &c : candidates)
      {
        if (fs::exists(c, ec) && fs::is_directory(c, ec))
          return fs::weakly_canonical(c);
      }

      return projectDir;
    }

    std::ifstream in(presetsPath);
    if (!in)
    {
      return projectDir;
    }

    nlohmann::json j;
    try
    {
      in >> j;
    }
    catch (...)
    {
      return projectDir;
    }

    try
    {
      if (!j.contains("configurePresets") || !j["configurePresets"].is_array())
        return projectDir;

      for (const auto &p : j["configurePresets"])
      {
        if (!p.is_object())
          continue;

        const std::string name = p.value("name", "");
        if (name != presetName)
          continue;

        const std::string binaryDir = p.value("binaryDir", "");
        if (!binaryDir.empty())
          return normalize_binary_dir(projectDir, binaryDir);

        const std::string buildDirectory = p.value("buildDirectory", "");
        if (!buildDirectory.empty())
          return normalize_binary_dir(projectDir, buildDirectory);

        return projectDir;
      }

      return projectDir;
    }
    catch (...)
    {
      return projectDir;
    }
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
      const bool needQuotes = (a.find(' ') != std::string::npos) || (a.find('\t') != std::string::npos);
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

  static int run_ctest(const vix::commands::TestsCommand::detail::Options &opt)
  {
    const std::string presetName = resolve_preset_name(opt);
    const fs::path buildDir = resolve_build_dir_or_fallback(opt.projectDir, presetName);

    std::error_code ec;
    if (!fs::exists(buildDir, ec) || !fs::is_directory(buildDir, ec))
    {
      error("Build directory does not exist.");
      hint("Run: vix check (or vix build) first to configure/build the project.");
      step(buildDir.string());
      return 1;
    }

    info("Running tests (CTest).");
    hint(std::string("Preset: ") + presetName);
    hint(std::string("Build dir: ") + buildDir.string());

    std::vector<std::string> argv;
    argv.push_back("ctest");

    for (const auto &a : opt.ctestArgs)
      argv.push_back(a);

    return run_in_dir(buildDir, argv);
  }

} // namespace

namespace vix::commands::TestsCommand
{
  int run(const std::vector<std::string> &args)
  {
    const auto opt = vix::commands::TestsCommand::detail::parse(args);

    if (!opt.watch)
    {
      const int code = run_ctest(opt);

      // --run (tests + runtime)
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
    hint("Flags: --list (ctest --show-only), --fail-fast, --run (tests + runtime)");

    g_stop.store(false);
    std::signal(SIGINT, on_sigint);

    const fs::path projectDir = opt.projectDir;

    StampMap prev = snapshot_tree(projectDir);
    int lastCode = run_ctest(opt);

    // debounce
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

        lastCode = run_ctest(opt);

        if (opt.runAfter)
        {
          if (lastCode == 0)
          {
            info("Runtime checks after tests (--run).");
            lastCode = vix::commands::CheckCommand::run(opt.forwarded);
          }
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
    out << "  Run project tests using CTest.\n";
    out << "  Build directory is resolved from CMakePresets.json (binaryDir).\n\n";

    out << "Tests flags:\n";
    out << "  --watch                   Watch files and re-run tests on changes\n";
    out << "  --list                    List tests (ctest --show-only)\n";
    out << "  --fail-fast               Stop on first failure (ctest --stop-on-failure)\n";
    out << "  --run                     Run runtime check after tests (tests + runtime)\n\n";

    out << "CTest passthrough:\n";
    out << "  Use `--` to pass raw arguments to ctest.\n";
    out << "  Example: vix tests -- --output-on-failure -R MySuite\n\n";

    out << "Notes:\n";
    out << "  - Preset is taken from forwarded args (e.g. --preset release)\n";
    out << "    or defaults to dev-ninja.\n";
    out << "  - All other options supported by `vix check` can still be forwarded.\n\n";

    out << "Examples:\n";
    out << "  vix tests\n";
    out << "  vix tests --watch\n";
    out << "  vix tests --list\n";
    out << "  vix tests --fail-fast\n";
    out << "  vix tests --run\n";
    out << "  vix tests ./examples/blog\n";
    out << "  vix tests --preset release\n\n";

    out << "See also:\n";
    out << "  vix check --tests\n";

    return 0;
  }
} // namespace vix::commands::TestsCommand
