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

#include <filesystem>
#include <unordered_map>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <iostream>

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

    // ignore common noise
    if (name == ".git" || name == ".idea" || name == ".vscode")
      return true;

    // ignore build dirs
    if (name.rfind("build", 0) == 0) // build, build-ninja, build-ninja-san...
      return true;

    // dist artifacts
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

    // Convert file_time_type to a stable integer stamp
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

} // namespace

namespace vix::commands::TestsCommand
{
  int run(const std::vector<std::string> &args)
  {
    const auto opt = vix::commands::TestsCommand::detail::parse(args);

    // One-shot mode
    if (!opt.watch)
    {
      info("Running tests (alias of `vix check --tests`).");
      return vix::commands::CheckCommand::run(opt.forwarded);
    }

    // Watch mode
    info("Watching project files and re-running tests on changes...");
    hint("Press Ctrl+C to stop.");
    hint("Flags: --list (ctest --show-only), --fail-fast, --run (tests + runtime)");

    g_stop.store(false);
    std::signal(SIGINT, on_sigint);

    const fs::path projectDir = opt.projectDir;

    // initial snapshot
    StampMap prev = snapshot_tree(projectDir);

    // run once immediately
    int lastCode = vix::commands::CheckCommand::run(opt.forwarded);

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

        // Wait debounce window for burst saves
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
        lastCode = vix::commands::CheckCommand::run(opt.forwarded);
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
    out << "  This command is a test-oriented alias of `vix check --tests`.\n\n";

    out << "Tests flags:\n";
    out << "  --watch                   Watch files and re-run tests on changes\n";
    out << "  --list                    List tests (ctest --show-only)\n";
    out << "  --fail-fast               Stop on first failure (ctest --stop-on-failure)\n";
    out << "  --run                     Run runtime check after tests (tests + runtime)\n\n";

    out << "Other options:\n";
    out << "  All options supported by `vix check` are supported here.\n";
    out << "  (presets, jobs, sanitizers, ctest preset, etc.)\n\n";

    out << "Examples:\n";
    out << "  vix tests\n";
    out << "  vix tests --watch\n";
    out << "  vix tests --list\n";
    out << "  vix tests --fail-fast\n";
    out << "  vix tests --run --san\n";
    out << "  vix tests ./examples/blog\n\n";

    out << "See also:\n";
    out << "  vix check --tests\n";

    return 0;
  }
}
