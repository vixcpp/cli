/**
 *
 *  @file BuildCommand.cpp
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
#include <vix/cli/ErrorHandler.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/cache/ArtifactCache.hpp>
#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/cli/commands/CloudCommand.hpp>
#include <vix/cli/build/BuildGraph.hpp>
#include <vix/cli/build/BuildScheduler.hpp>
#include <vix/cli/build/ObjectCache.hpp>
#include <vix/cli/build/BuildGraphExecutor.hpp>
#include <vix/cli/build/BuildGraphExecutorAdapter.hpp>
#include <vix/cli/build/BuildLiveProcess.hpp>
#include <vix/cli/build/BuildTaskProcessExecutor.hpp>
#include <vix/cli/build/BuildStyle.hpp>
#include <vix/cli/build/BuildContext.hpp>
#include <vix/cli/app/AppManifest.hpp>
#include <vix/cli/app/AppCMakeGenerator.hpp>
#include <vix/cli/app/AppProjectResolver.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include <vix/cli/cmake/CMakeBuild.hpp>
#include <vix/cli/cmake/GlobalPackages.hpp>
#include <vix/cli/cmake/Toolchain.hpp>
#include <vix/cli/util/Args.hpp>
#include <vix/cli/util/Console.hpp>
#include <vix/cli/util/Fs.hpp>
#include <vix/cli/util/Hash.hpp>
#include <vix/cli/util/Strings.hpp>
#include <vix/cli/util/Ui.hpp>

#include <vix/cli/commands/run/detail/ScriptProbe.hpp>
#include <vix/cli/commands/run/detail/DirectScriptRunner.hpp>
#include <vix/cli/commands/run/detail/ScriptCMake.hpp>
#include <vix/cli/commands/run/RunDetail.hpp>
#include <vix/cli/sdk/SdkProfiles.hpp>
#include <vix/engine/BuildTools.hpp>
#include <vix/engine/CMakeConfiguration.hpp>
#include <vix/engine/ConfigurationSignature.hpp>
#include <vix/engine/ConfigureDecision.hpp>
#include <vix/engine/ExecutionPlanning.hpp>
#include <vix/engine/Watch.hpp>
#include <vix/engine/SanitizerMode.hpp>

namespace fs = std::filesystem;
using namespace vix::cli::style;
namespace process = vix::cli::process;
namespace util = vix::cli::util;
namespace build = vix::cli::build;
namespace artifact_cache = vix::cli::cache;
namespace run_detail = vix::commands::RunCommand::detail;
namespace app = vix::cli::app;

namespace vix::commands::BuildCommand
{
  namespace
  {
    static constexpr std::uint64_t LOCAL_FNV_OFFSET = 1469598103934665603ull;
    static volatile std::sig_atomic_t g_watch_stop_requested = 0;
    static constexpr const char *WATCH_PRIMARY = "\033[97m";

    static void on_watch_signal(int)
    {
      g_watch_stop_requested = 1;
    }

    struct WatchCapturedRun
    {
      int code{0};
      std::string diagnostics;
    };

    struct WatchDisplayContext
    {
      fs::path projectDir;
      bool quiet{false};
      bool verbose{false};
    };

    struct ResolvedBuildPlan
    {
      process::Plan plan;
      std::string sdkResolutionError;
    };

    enum class WatchDisplayAction
    {
      Rebuilt,
      Reconfigured
    };

    static std::string watch_format_duration(long long ms)
    {
      if (ms > 0 && ms < 1000)
        return std::to_string(ms) + "ms";

      return util::format_seconds(ms);
    }

    static std::string watch_relative_path(
        const fs::path &path,
        const fs::path &projectDir)
    {
      std::error_code ec;
      fs::path relative = fs::relative(path, projectDir, ec);

      if (ec || relative.empty())
        relative = path.lexically_normal().lexically_relative(projectDir);

      if (relative.empty())
        relative = path.filename();

      return relative.lexically_normal().generic_string();
    }

    static bool watch_is_config_path(const fs::path &path)
    {
      const std::string name = path.filename().string();
      const std::string ext = path.extension().string();

      return name == "CMakeLists.txt" ||
             name == "CMakePresets.json" ||
             name == "CMakeUserPresets.json" ||
             name == "vix.app" ||
             name == "vix.json" ||
             name == "vix.toml" ||
             name == "vix.lock" ||
             name == "vix.module" ||
             ext == ".cmake";
    }

    static std::vector<vix::engine::watch::Event> watch_effective_events(
        const vix::engine::watch::Batch &batch)
    {
      std::vector<vix::engine::watch::Event> events;

      for (const auto &event : batch.events)
      {
        if (event.kind == vix::engine::watch::EventKind::Overflow)
          continue;
        if (event.path.empty())
          continue;
        events.push_back(event);
      }

      std::sort(
          events.begin(),
          events.end(),
          [](const auto &a, const auto &b)
          {
            return a.path.lexically_normal().generic_string() <
                   b.path.lexically_normal().generic_string();
          });

      return events;
    }

    static std::string watch_change_subject(
        const vix::engine::watch::Batch &batch,
        const fs::path &projectDir,
        bool configurationChange,
        bool structuralChange)
    {
      const auto events = watch_effective_events(batch);

      if (events.empty())
      {
        if (configurationChange)
          return "build configuration";
        if (structuralChange)
          return "project structure";
        return "project";
      }

      if (events.size() == 1)
      {
        const auto &event = events.front();
        if (event.kind == vix::engine::watch::EventKind::Renamed &&
            !event.oldPath.empty())
        {
          return watch_relative_path(event.oldPath, projectDir) +
                 " -> " +
                 watch_relative_path(event.path, projectDir);
        }

        return watch_relative_path(event.path, projectDir);
      }

      if (configurationChange)
        return "build configuration";
      if (structuralChange)
        return "project structure";

      return std::to_string(events.size()) + " files";
    }

    static bool watch_batch_has_configuration_path(
        const vix::engine::watch::Batch &batch)
    {
      for (const auto &event : batch.events)
      {
        if (watch_is_config_path(event.path) ||
            (!event.oldPath.empty() && watch_is_config_path(event.oldPath)))
        {
          return true;
        }
      }

      return batch.overflowed;
    }

    static bool watch_path_within(
        const fs::path &path,
        const fs::path &root)
    {
      const fs::path p = path.lexically_normal();
      const fs::path r = root.lexically_normal();

      auto pit = p.begin();
      auto rit = r.begin();

      for (; rit != r.end(); ++rit, ++pit)
      {
        if (pit == p.end() || *pit != *rit)
          return false;
      }

      return true;
    }

    static bool watch_content_fingerprint_skip_path(
        const fs::path &path,
        const std::vector<fs::path> &ignoredRoots)
    {
      const fs::path normalized = path.lexically_normal();

      for (const fs::path &root : ignoredRoots)
      {
        if (!root.empty() && watch_path_within(normalized, root))
          return true;
      }

      for (const fs::path &part : normalized)
      {
        const std::string item = part.string();
        if (item == ".git" ||
            item == ".hg" ||
            item == ".svn" ||
            item == ".vix" ||
            item == "node_modules" ||
            item == ".cache" ||
            item == ".idea" ||
            item == ".vscode" ||
            item == "CMakeFiles")
        {
          return true;
        }
      }

      const std::string name = normalized.filename().string();
      return name == "compile_commands.json" ||
             name == "build.ninja" ||
             name == "CMakeCache.txt" ||
             name == "configure.log" ||
             name == "build.log";
    }

    class WatchContentFingerprints
    {
    public:
      WatchContentFingerprints(
          fs::path root,
          std::vector<fs::path> ignoredRoots)
          : root_(std::move(root)),
            ignoredRoots_(std::move(ignoredRoots))
      {
      }

      void seed()
      {
        std::error_code ec;
        root_ = fs::absolute(root_, ec).lexically_normal();
        if (ec || root_.empty() || !fs::exists(root_, ec))
          return;

        if (fs::is_regular_file(root_, ec))
        {
          remember(root_);
          return;
        }

        fs::recursive_directory_iterator it(
            root_,
            fs::directory_options::skip_permission_denied,
            ec);
        const fs::recursive_directory_iterator end;

        while (!ec && it != end)
        {
          const fs::path current = it->path().lexically_normal();

          if (watch_content_fingerprint_skip_path(current, ignoredRoots_))
          {
            if (it->is_directory(ec))
              it.disable_recursion_pending();
            ++it;
            continue;
          }

          if (it->is_regular_file(ec))
            remember(current);

          ++it;
        }
      }

      vix::engine::watch::Batch filter(
          const vix::engine::watch::Batch &batch)
      {
        if (batch.empty() || batch.overflowed)
          return batch;

        vix::engine::watch::Batch filtered;
        std::map<std::string, std::optional<std::string>> batchHashes;

        for (const auto &event : batch.events)
        {
          if (event.directory ||
              event.kind == vix::engine::watch::EventKind::Overflow)
          {
            filtered.events.push_back(event);
            continue;
          }

          const fs::path path = event.path.lexically_normal();
          const std::string key = path.generic_string();

          if (event.kind == vix::engine::watch::EventKind::Removed)
          {
            hashes_.erase(key);
            filtered.events.push_back(event);
            continue;
          }

          if (event.kind == vix::engine::watch::EventKind::Added)
          {
            const std::optional<std::string> hash =
                hash_from_cache(path, batchHashes);

            if (!hash)
            {
              hashes_.erase(key);
              filtered.events.push_back(event);
              continue;
            }

            const auto previous = hashes_.find(key);
            const bool unchanged =
                previous != hashes_.end() && previous->second == *hash;

            hashes_[key] = *hash;

            if (unchanged)
              continue;

            filtered.events.push_back(event);
            continue;
          }

          if (event.kind != vix::engine::watch::EventKind::Modified &&
              event.kind != vix::engine::watch::EventKind::Renamed)
          {
            remember_from_cache(path, batchHashes);
            filtered.events.push_back(event);
            continue;
          }

          const std::optional<std::string> hash =
              hash_from_cache(path, batchHashes);

          if (!hash)
          {
            hashes_.erase(key);
            filtered.events.push_back(event);
            continue;
          }

          const auto previous = hashes_.find(key);
          const bool unchanged =
              previous != hashes_.end() && previous->second == *hash;

          hashes_[key] = *hash;

          if (event.kind == vix::engine::watch::EventKind::Renamed &&
              !event.oldPath.empty())
          {
            hashes_.erase(event.oldPath.lexically_normal().generic_string());
          }

          if (unchanged)
            continue;

          filtered.events.push_back(event);
        }

        return filtered;
      }

    private:
      void remember(const fs::path &path)
      {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec) || ec)
          return;

        const auto hash = util::read_file_hash_hex(path);
        if (hash)
          hashes_[path.lexically_normal().generic_string()] = *hash;
      }

      std::optional<std::string> hash_from_cache(
          const fs::path &path,
          std::map<std::string, std::optional<std::string>> &batchHashes)
      {
        const std::string key = path.lexically_normal().generic_string();
        const auto cached = batchHashes.find(key);
        if (cached != batchHashes.end())
          return cached->second;

        std::error_code ec;
        std::optional<std::string> hash;
        if (fs::is_regular_file(path, ec) && !ec)
          hash = util::read_file_hash_hex(path);

        batchHashes.emplace(key, hash);
        return hash;
      }

      void remember_from_cache(
          const fs::path &path,
          std::map<std::string, std::optional<std::string>> &batchHashes)
      {
        const auto hash = hash_from_cache(path, batchHashes);
        if (hash)
          hashes_[path.lexically_normal().generic_string()] = *hash;
      }

      fs::path root_;
      std::vector<fs::path> ignoredRoots_;
      std::map<std::string, std::string> hashes_;
    };

    static void watch_print_line(
        const WatchDisplayContext &display,
        const std::string &line,
        std::ostream &out = std::cout)
    {
      if (display.quiet)
        return;

      out << line << "\n";
      out.flush();
    }

    static bool watch_stdout_is_tty()
    {
#ifdef _WIN32
      return ::_isatty(1) != 0;
#else
      return ::isatty(STDOUT_FILENO) != 0;
#endif
    }

    static int watch_terminal_columns()
    {
#ifdef _WIN32
      return 100;
#else
      struct winsize ws{};

      if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return static_cast<int>(ws.ws_col);

      return 100;
#endif
    }

    static std::string watch_truncate_plain(
        const std::string &value,
        int width)
    {
      if (width <= 0)
        return {};

      if (static_cast<int>(value.size()) <= width)
        return value;

      if (width <= 3)
        return value.substr(0, static_cast<std::size_t>(width));

      return "..." + value.substr(value.size() - static_cast<std::size_t>(width - 3));
    }

    static std::string watch_fit_terminal_subject(
        const std::string &subject,
        int reservedColumns)
    {
      if (!watch_stdout_is_tty())
        return subject;

      const int columns = watch_terminal_columns();
      const int subjectWidth = std::max(12, columns - reservedColumns);
      return watch_truncate_plain(subject, subjectWidth);
    }

    static std::string watch_label(
        const char *color,
        const std::string &label)
    {
      std::ostringstream out;

      out << color << BOLD
          << label
          << RESET
          << " ";

      return out.str();
    }

    static const char *watch_duration_color(long long ms)
    {
      if (ms >= 10000)
        return RED;

      if (ms >= 3000)
        return YELLOW;

      return GREEN;
    }

    static void watch_print_header(
        const WatchDisplayContext &display,
        const std::string &target)
    {
      std::ostringstream line;

      line << watch_label(CYAN, "Watching")
           << WATCH_PRIMARY << BOLD << target << RESET;

      watch_print_line(display, line.str());
    }

    static void watch_print_waiting(
        const WatchDisplayContext &display,
        std::ostream &out = std::cout)
    {
      std::ostringstream line;

      line << watch_label(GRAY, "Waiting")
           << GRAY << "for changes"
           << " "
           << "(Ctrl+C to stop)"
           << RESET;

      watch_print_line(display, line.str(), out);
    }

    static std::string watch_normalize_linker(std::string linker)
    {
      constexpr std::string_view prefix = "-fuse-ld=";

      if (linker.rfind(prefix, 0) == 0)
        return linker.substr(prefix.size());

      return linker;
    }

    static std::string watch_relative_build_dir(
        const WatchDisplayContext &display,
        const fs::path &buildDir)
    {
      const fs::path normalizedBuildDir =
          fs::absolute(buildDir).lexically_normal();
      const fs::path normalizedProjectDir =
          fs::absolute(display.projectDir).lexically_normal();

      const std::string build = normalizedBuildDir.string();
      const std::string project = normalizedProjectDir.string();

      if (!project.empty() &&
          build.size() > project.size() &&
          build.compare(0, project.size(), project) == 0 &&
          build[project.size()] == fs::path::preferred_separator)
      {
        return fs::relative(normalizedBuildDir, normalizedProjectDir).string();
      }

      return buildDir.string();
    }

    static void watch_print_session_metadata(
        const WatchDisplayContext &display,
        const std::string &backend,
        const fs::path &buildDir,
        const std::string &target,
        int jobs,
        const std::optional<std::string> &launcher,
        const std::optional<std::string> &linker)
    {
      if (display.quiet)
        return;

      std::ostringstream first;
      first << "  " << CYAN << "backend " << RESET
            << WATCH_PRIMARY << backend << RESET
            << YELLOW << " · " << RESET
            << CYAN << "target " << RESET
            << WATCH_PRIMARY << target << RESET
            << YELLOW << " · " << RESET
            << CYAN << "jobs " << RESET
            << WATCH_PRIMARY << jobs << RESET;
      watch_print_line(display, first.str());

      std::ostringstream second;
      second << "  " << CYAN << "build " << RESET
             << WATCH_PRIMARY << watch_relative_build_dir(display, buildDir)
             << RESET;

      if (launcher)
        second << YELLOW << " · " << RESET
               << CYAN << "launcher " << RESET
               << WATCH_PRIMARY << *launcher << RESET;

      if (linker)
        second << YELLOW << " · " << RESET
               << CYAN << "linker " << RESET
               << WATCH_PRIMARY << watch_normalize_linker(*linker) << RESET;

      watch_print_line(display, second.str());
    }

    static void watch_print_explain_affected_tasks(
        const WatchDisplayContext &display,
        std::size_t affectedTasks)
    {
      std::ostringstream line;
      line << "  " << YELLOW << affectedTasks << RESET
           << GRAY << " affected tasks" << RESET;
      watch_print_line(display, line.str());
    }

    static void watch_finish_terminal(
        const WatchDisplayContext &display)
    {
      if (display.quiet)
        return;

      std::cout << RESET;

      if (watch_stdout_is_tty())
        std::cout << "\n";

      std::cout << std::flush;
    }

    class WatchProgressLine
    {
    public:
      WatchProgressLine(
          const WatchDisplayContext &display,
          const vix::engine::watch::Batch &batch,
          WatchDisplayAction action,
          bool structuralChange,
          std::string detail = {})
          : active_(!display.quiet && watch_stdout_is_tty()),
            subject_(
                watch_change_subject(
                    batch,
                    display.projectDir,
                    action == WatchDisplayAction::Reconfigured,
                    structuralChange)),
            action_(action),
            detail_(std::move(detail)),
            started_(std::chrono::steady_clock::now())
      {
        verb_ =
            action_ == WatchDisplayAction::Reconfigured
                ? "Configuring"
                : "Building";

        if (!active_)
          return;

        worker_ = std::thread(
            [this]()
            {
              while (!stop_.load())
              {
                render();
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
              }
            });
      }

      ~WatchProgressLine()
      {
        stop();
      }

      WatchProgressLine(const WatchProgressLine &) = delete;
      WatchProgressLine &operator=(const WatchProgressLine &) = delete;

      void update(
          std::string verb,
          std::string subject,
          std::string detail = {})
      {
        if (!active_)
          return;

        std::lock_guard<std::mutex> lock(mutex_);
        verb_ = std::move(verb);
        subject_ = std::move(subject);
        detail_ = std::move(detail);
      }

      void stop()
      {
        if (!active_)
          return;

        stop_.store(true);

        if (worker_.joinable())
          worker_.join();

        std::cout << "\r\033[2K" << std::flush;
        active_ = false;
      }

    private:
      void render()
      {
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started_)
                .count();

        std::string verb;
        std::string subject;
        std::string detail;

        {
          std::lock_guard<std::mutex> lock(mutex_);
          verb = verb_;
          subject = subject_;
          detail = detail_;
        }

        const int columns = watch_terminal_columns();
        const std::string duration = watch_format_duration(ms);
        const int reservedColumns =
            static_cast<int>(
                verb.size() +
                std::string("  ").size() +
                duration.size() +
                (detail.empty() ? 0 : detail.size() + 3));
        const int subjectWidth = std::max(12, columns - reservedColumns);

        std::ostringstream line;
        line << verb << " "
             << watch_truncate_plain(subject, subjectWidth);

        if (!detail.empty())
          line << " (" << detail << ")";

        line << " " << duration;

        std::cout << "\r\033[2K"
                  << watch_truncate_plain(line.str(), columns)
                  << std::flush;
      }

      bool active_{false};
      std::atomic<bool> stop_{false};
      std::thread worker_;
      std::mutex mutex_;
      std::string verb_{"Building"};
      std::string subject_;
      WatchDisplayAction action_{WatchDisplayAction::Rebuilt};
      std::string detail_;
      std::chrono::steady_clock::time_point started_;
    };

    struct BuildPhaseTiming
    {
      std::string name;
      long long ms{0};
    };

    static void build_print_phase_timings(
        const std::vector<BuildPhaseTiming> &timings)
    {
      if (timings.empty())
        return;

      for (const BuildPhaseTiming &timing : timings)
      {
        std::cout << timing.name << ": "
                  << watch_format_duration(timing.ms)
                  << "\n";
      }

      std::cout.flush();
    }

    static bool watch_is_ninja_progress_line(const std::string &line)
    {
      if (line.empty() || line.front() != '[')
        return false;

      const std::size_t slash = line.find('/');
      const std::size_t close = line.find(']');

      return slash != std::string::npos &&
             close != std::string::npos &&
             slash < close;
    }

    static std::string watch_filter_failure_diagnostics(
        const std::string &diagnostics)
    {
      std::istringstream in(diagnostics);
      std::ostringstream out;
      std::string line;

      while (std::getline(in, line))
      {
        if (line.rfind("ninja: Entering directory", 0) == 0)
          continue;
        if (line == "ninja: build stopped: subcommand failed.")
          continue;
        if (line.rfind("ninja -C ", 0) == 0)
          continue;
        if (line.rfind("FAILED: ", 0) == 0)
          continue;
        if (line.rfind("Unable to resolve a unique graph output target:", 0) == 0)
          continue;
        if (watch_is_ninja_progress_line(line))
          continue;
        if (line.rfind("/", 0) == 0 &&
            line.find(" -c ") != std::string::npos)
          continue;
        if (line.rfind("ccache ", 0) == 0 &&
            line.find(" -c ") != std::string::npos)
          continue;

        out << line << "\n";
      }

      return out.str();
    }

    static void watch_print_ready(
        const WatchDisplayContext &display,
        const std::string &target,
        long long ms)
    {
      watch_print_header(display, target);

      std::ostringstream status;

      status << watch_label(GREEN, "Finished")
             << "initial build"
             << GRAY << " in " << RESET
             << watch_duration_color(ms)
             << BOLD
             << watch_format_duration(ms)
             << RESET;

      watch_print_line(display, status.str());
      watch_print_waiting(display);
    }

    static void watch_print_initial_done(
        const WatchDisplayContext &display,
        long long ms)
    {
      std::ostringstream status;

      status << watch_label(GREEN, "Finished")
             << "initial build"
             << GRAY << " in " << RESET
             << watch_duration_color(ms)
             << BOLD
             << watch_format_duration(ms)
             << RESET;

      watch_print_line(display, status.str());
    }

    static void watch_print_initial_failed(
        const WatchDisplayContext &display,
        const std::string &target,
        const std::string &diagnostics)
    {
      watch_print_header(display, target);

      std::ostringstream status;

      status << watch_label(RED, "Error")
             << RED << BOLD << "initial build failed" << RESET;

      watch_print_line(display, status.str(), std::cerr);

      const std::string filtered =
          watch_filter_failure_diagnostics(diagnostics);

      if (!display.quiet && !filtered.empty())
      {
        std::cerr << filtered;

        if (filtered.back() != '\n')
          std::cerr << "\n";

        std::cerr.flush();
      }

      watch_print_waiting(display, std::cerr);
    }

    static void watch_print_completed(
        const WatchDisplayContext &display,
        const vix::engine::watch::Batch &batch,
        WatchDisplayAction action,
        bool structuralChange,
        long long ms,
        std::string detail = {},
        std::string subjectOverride = {})
    {
      const bool configurationChange =
          action == WatchDisplayAction::Reconfigured;

      const std::string subject =
          subjectOverride.empty()
              ? watch_change_subject(
                    batch,
                    display.projectDir,
                    configurationChange,
                    structuralChange)
              : std::move(subjectOverride);

      const char *labelColor = GREEN;
      const char *verb = "";

      if (configurationChange)
      {
        labelColor = CYAN;
        verb = "reconfigured";
      }
      else if (structuralChange)
      {
        labelColor = YELLOW;
      }

      const std::string duration = watch_format_duration(ms);
      const std::string verbText = verb;
      const int reservedColumns =
          static_cast<int>(
              std::string("Finished ").size() +
              verbText.size() +
              (verbText.empty() ? 0 : 1) +
              std::string(" in ").size() +
              duration.size() +
              (detail.empty() ? 0 : detail.size() + 3));

      const std::string fittedSubject =
          watch_fit_terminal_subject(subject, reservedColumns);

      std::ostringstream line;

      line << watch_label(labelColor, "Finished")
           << (verbText.empty() ? "" : verbText + " ")
           << WATCH_PRIMARY << BOLD << fittedSubject << RESET
           << GRAY << " in " << RESET
           << watch_duration_color(ms)
           << BOLD
           << duration
           << RESET;

      if (!detail.empty())
        line << GRAY << " (" << detail << ")" << RESET;

      watch_print_line(display, line.str());
    }

    static void watch_print_failed(
        const WatchDisplayContext &display,
        const vix::engine::watch::Batch &batch,
        WatchDisplayAction action,
        bool structuralChange,
        const std::string &diagnostics)
    {
      const bool configurationChange =
          action == WatchDisplayAction::Reconfigured;

      const std::string subject =
          watch_change_subject(
              batch,
              display.projectDir,
              configurationChange,
              structuralChange);

      const std::string actionLabel =
          configurationChange
              ? "reconfiguring"
              : "rebuilding";

      const int reservedColumns =
          static_cast<int>(
              std::string("Error ").size() +
              actionLabel.size() +
              1);

      const std::string fittedSubject =
          watch_fit_terminal_subject(subject, reservedColumns);

      std::ostringstream line;

      line << watch_label(RED, "Error")
           << RED << BOLD << actionLabel << RESET
           << " "
           << WATCH_PRIMARY << BOLD << fittedSubject << RESET;

      watch_print_line(display, line.str(), std::cerr);

      const std::string filtered =
          watch_filter_failure_diagnostics(diagnostics);

      if (!display.quiet && !filtered.empty())
      {
        std::cerr << filtered;

        if (filtered.back() != '\n')
          std::cerr << "\n";

        std::cerr.flush();
      }

      watch_print_waiting(display, std::cerr);
    }

    template <typename Fn>
    static WatchCapturedRun watch_run_capturing_stderr(
        bool capture,
        Fn &&fn)
    {
      if (!capture)
        return {fn(), {}};

      std::ostringstream diagnostics;
      std::streambuf *old = std::cerr.rdbuf(diagnostics.rdbuf());
      const int code = fn();
      std::cerr.rdbuf(old);

      return {code, diagnostics.str()};
    }

    static std::string platform_executable_name(const std::string &name);

    static void write_last_binary(const fs::path &path);

    static void write_project_build_metadata(
        const fs::path &projectDir,
        const fs::path &buildDir,
        const fs::path &binary,
        vix::engine::SanitizerMode sanitizerMode);
    ;

    static void resolve_sdk_for_plan(
        process::Plan &plan,
        std::string &sdkResolutionError,
        const process::Options &opt);

    static std::optional<fs::path> resolve_main_executable(
        const fs::path &buildDir,
        const fs::path &projectDir,
        const std::string &buildTarget,
        const std::string &defaultTargetName);

    struct DeferredConsole
    {
      bool enabled{false};
      std::ostringstream buf;

      explicit DeferredConsole(bool on) : enabled(on) {}

      void print(const std::string &s)
      {
        if (enabled)
          buf << s;
        else
          std::cout << s;
      }

      void flush_to_stdout()
      {
        if (!enabled)
          return;
        std::cout << buf.str();
        buf.str("");
        buf.clear();
      }

      void discard()
      {
        buf.str("");
        buf.clear();
      }
    };

    static bool write_if_different(const fs::path &path, const std::string &content)
    {
      if (util::file_exists(path))
      {
        const std::string current = util::read_text_file_or_empty(path);
        if (current == content)
          return true;
      }

      return util::write_text_file_atomic(path, content);
    }

    static bool graph_executor_enabled(const process::Options &opt)
    {
      if (opt.graphExecutor == "on")
        return true;
      if (opt.graphExecutor == "off")
        return false;
      if (opt.graphExecutorExplicit)
        return true;

      const char *value = std::getenv("VIX_GRAPH_EXECUTOR");

      if (!value || !*value)
        return true;

      std::string s(value);

      for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      return !(s == "0" ||
               s == "false" ||
               s == "no" ||
               s == "off");
    }

    static bool is_all_build_target(const std::string &target)
    {
      return target.empty() || target == "all";
    }

    static bool can_use_target_graph_executor(
        const process::Options &opt,
        const std::size_t importedCompileCommands,
        const std::size_t importedNinjaTasks)
    {
      if (!graph_executor_enabled(opt))
        return false;

      if (!opt.useCache)
        return false;

      if (opt.clean)
        return false;

      if (is_all_build_target(opt.buildTarget))
        return false;

      if (!opt.targetTriple.empty())
        return false;

      if (!opt.cmakeArgs.empty())
        return false;

      if (opt.withSqlite || opt.withMySql)
        return false;

      if (opt.linkStatic)
        return false;

      if (importedCompileCommands == 0)
        return false;

      if (importedNinjaTasks == 0)
        return false;

      return true;
    }

    static std::string sanitize_cache_component(std::string s)
    {
      for (char &c : s)
      {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '.' || c == '_' || c == '-' || c == '+'))
          c = '_';
      }

      if (s.empty())
        return "unknown";

      return s;
    }

    static std::string detect_native_target_triple()
    {
#if defined(__x86_64__) && defined(__linux__)
      return "x86_64-linux-gnu";
#elif defined(__aarch64__) && defined(__linux__)
      return "aarch64-linux-gnu";
#elif defined(__arm__) && defined(__linux__)
      return "arm-linux-gnueabihf";
#elif defined(__riscv) && (__riscv_xlen == 64) && defined(__linux__)
      return "riscv64-linux-gnu";
#elif defined(_WIN32) && defined(_M_X64)
      return "x86_64-windows-msvc";
#elif defined(_WIN32) && defined(_M_ARM64)
      return "aarch64-windows-msvc";
#elif defined(__APPLE__) && defined(__aarch64__)
      return "aarch64-apple-darwin";
#elif defined(__APPLE__) && defined(__x86_64__)
      return "x86_64-apple-darwin";
#else
      return "unknown-target";
#endif
    }

    static std::string detect_compiler_identity()
    {
#ifdef __clang__
      return sanitize_cache_component(
          "clang++-" + std::to_string(__clang_major__) + "." +
          std::to_string(__clang_minor__) + "." +
          std::to_string(__clang_patchlevel__));
#elif defined(__GNUC__)
      return sanitize_cache_component(
          "g++-" + std::to_string(__GNUC__) + "." +
          std::to_string(__GNUC_MINOR__) + "." +
          std::to_string(__GNUC_PATCHLEVEL__));
#elif defined(_MSC_VER)
      return sanitize_cache_component("msvc-" + std::to_string(_MSC_VER));
#else
      return "unknown-compiler";
#endif
    }

    static std::string make_artifact_fingerprint(
        const process::Plan &plan,
        const process::Options &opt,
        const std::string &toolchainContent)
    {
      std::ostringstream oss;

      oss << "project=" << plan.projectDir.string() << "\n";
      oss << "preset=" << plan.preset.name << "\n";
      oss << "buildType=" << plan.preset.buildType << "\n";
      oss << "buildTarget=" << opt.buildTarget << "\n";
      oss << "defaultTargetName=" << plan.defaultTargetName << "\n";
      oss << "projectFingerprint=" << plan.projectFingerprint << "\n";
      oss << "targetTriple=" << opt.targetTriple << "\n";
      oss << "sysroot=" << opt.sysroot << "\n";
      oss << "static=" << (opt.linkStatic ? "1" : "0") << "\n";
      oss << "linker=" << static_cast<int>(opt.linker) << "\n";
      oss << "launcher=" << static_cast<int>(opt.launcher) << "\n";

      if (plan.fastLinkerFlag)
        oss << "fastLinkerFlag=" << *plan.fastLinkerFlag << "\n";

      if (plan.launcher)
        oss << "launcherTool=" << *plan.launcher << "\n";

      oss << "vars:\n";
      oss << util::signature_join(plan.cmakeVars);

      oss << "rawCMakeArgs:\n";
      for (const auto &arg : opt.cmakeArgs)
        oss << arg << "\n";

      if (!toolchainContent.empty())
      {
        oss << "toolchain:\n";
        oss << toolchainContent;

        if (toolchainContent.back() != '\n')
          oss << "\n";
      }

      const std::string payload = oss.str();
      const std::uint64_t h = util::fnv1a64_str(payload, LOCAL_FNV_OFFSET);

      return util::hex64(h);
    }

    static std::string make_object_cache_build_fingerprint(
        const process::Plan &plan,
        const process::Options &opt)
    {
      std::ostringstream oss;

      oss << "signature=" << plan.signature << "\n";
      oss << "preset=" << plan.preset.name << "\n";
      oss << "buildType=" << plan.preset.buildType << "\n";
      oss << "targetTriple="
          << (opt.targetTriple.empty() ? detect_native_target_triple() : opt.targetTriple)
          << "\n";
      oss << "compiler=" << detect_compiler_identity() << "\n";
      oss << "linker=" << static_cast<int>(opt.linker) << "\n";
      oss << "launcher=" << static_cast<int>(opt.launcher) << "\n";

      if (plan.launcher)
        oss << "launcherTool=" << *plan.launcher << "\n";

      if (plan.fastLinkerFlag)
        oss << "fastLinkerFlag=" << *plan.fastLinkerFlag << "\n";

      oss << "cmakeVars:\n";
      oss << util::signature_join(plan.cmakeVars);

      oss << "rawCMakeArgs:\n";
      for (const auto &arg : opt.cmakeArgs)
        oss << arg << "\n";

      const std::string payload = oss.str();
      const std::uint64_t h = util::fnv1a64_str(payload, LOCAL_FNV_OFFSET);

      return util::hex64(h);
    }

    static artifact_cache::Artifact make_project_artifact(
        const process::Plan &plan,
        const process::Options &opt,
        const std::string &toolchainContent)
    {
      artifact_cache::Artifact a;
      a.package = sanitize_cache_component(
          !plan.defaultTargetName.empty()
              ? plan.defaultTargetName
              : plan.projectDir.filename().string());
      a.version = "local";
      a.target = sanitize_cache_component(
          opt.targetTriple.empty() ? detect_native_target_triple() : opt.targetTriple);
      a.compiler = detect_compiler_identity();
      a.buildType = sanitize_cache_component(plan.preset.buildType);
      a.fingerprint = make_artifact_fingerprint(plan, opt, toolchainContent);

      const fs::path root = artifact_cache::ArtifactCache::artifact_path(a);
      a.root = root;
      a.include = root / "include";
      a.lib = root / "lib";

      return a;
    }

    static bool persist_project_artifact(const artifact_cache::Artifact &a)
    {
      if (!artifact_cache::ArtifactCache::ensure_layout(a))
        return false;

      return artifact_cache::ArtifactCache::write_manifest(a);
    }

    static bool can_use_target_artifact_cache(const process::Options &opt)
    {
      if (!opt.useCache)
        return false;

      if (opt.explain)
        return false;

      if (opt.clean)
        return false;

      if (opt.exportBin || !opt.outPath.empty())
        return false;

      if (is_all_build_target(opt.buildTarget))
        return false;

      if (!opt.cmakeArgs.empty())
        return false;

      if (!opt.targetTriple.empty())
        return false;

      if (opt.withSqlite || opt.withMySql)
        return false;

      if (opt.linkStatic)
        return false;

      return true;
    }

    static fs::path artifact_target_binary_path(
        const artifact_cache::Artifact &artifact,
        const process::Options &opt,
        const process::Plan &plan)
    {
      const std::string target =
          build::default_build_target_name(opt, plan);

      return artifact.root / "bin" / platform_executable_name(target);
    }

    static bool copy_executable_file(
        const fs::path &source,
        const fs::path &destination)
    {
      std::error_code ec;

      if (!fs::exists(source, ec) || ec)
        return false;

      if (!fs::is_regular_file(source, ec) || ec)
        return false;

      const fs::path parent = destination.parent_path();

      if (!parent.empty())
      {
        fs::create_directories(parent, ec);

        if (ec)
          return false;
      }

      fs::copy_file(
          source,
          destination,
          fs::copy_options::overwrite_existing,
          ec);

      if (ec)
        return false;

#ifndef _WIN32
      const auto perms = fs::status(source, ec).permissions();

      if (!ec)
      {
        fs::permissions(
            destination,
            perms,
            fs::perm_options::replace,
            ec);
      }
#endif

      return true;
    }

    static bool restore_project_target_artifact(
        const artifact_cache::Artifact &artifact,
        const process::Options &opt,
        const process::Plan &plan)
    {
      const fs::path cachedBinary =
          artifact_target_binary_path(artifact, opt, plan);

      const fs::path destination =
          build::default_project_executable_path(opt, plan);

      if (!copy_executable_file(cachedBinary, destination))
        return false;

      if (!persist_project_artifact(artifact))
        return false;

      write_project_build_metadata(
          plan.userProjectDir,
          plan.buildDir,
          destination,
          opt.sanitizerMode);

      return true;
    }

    static bool store_project_target_artifact(
        const artifact_cache::Artifact &artifact,
        const process::Options &opt,
        const process::Plan &plan)
    {
      const auto exeOpt = resolve_main_executable(
          plan.buildDir,
          plan.userProjectDir,
          opt.buildTarget,
          plan.defaultTargetName);

      if (!exeOpt)
        return false;

      if (!artifact_cache::ArtifactCache::ensure_layout(artifact))
        return false;

      const fs::path cachedBinary =
          artifact_target_binary_path(artifact, opt, plan);

      if (!copy_executable_file(*exeOpt, cachedBinary))
        return false;

      return artifact_cache::ArtifactCache::write_manifest(artifact);
    }

    static bool select_sanitizer_mode(
        process::Options &options,
        std::optional<process::SanitizerMode> &selectedMode,
        std::string &selectedArgument,
        process::SanitizerMode requestedMode,
        std::string_view requestedArgument,
        int &exitCode)
    {
      if (selectedMode &&
          *selectedMode != requestedMode)
      {
        error(
            "Conflicting sanitizer options: " +
            selectedArgument +
            " and " +
            std::string(requestedArgument));

        hint("Choose exactly one sanitizer mode per build.");
        hint("Recommended: vix build --sanitize");

        exitCode = 2;
        return false;
      }

      selectedMode = requestedMode;
      selectedArgument = std::string(requestedArgument);
      options.sanitizerMode = requestedMode;

      return true;
    }

    static process::Options parse_args_or_exit(
        const std::vector<std::string> &args,
        int &exitCode)
    {
      process::Options o;
      exitCode = 0;
      std::optional<process::SanitizerMode> selectedSanitizerMode;
      std::string selectedSanitizerArgument;

      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &a = args[i];

        if (a == "--")
        {
          for (std::size_t j = i + 1; j < args.size(); ++j)
            o.cmakeArgs.push_back(args[j]);
          break;
        }

        if (a == "--help" || a == "-h")
        {
          exitCode = -2;
          return o;
        }
        else if (a == "--verbose" || a == "-v")
        {
          o.verbose = true;
        }
        else if (a == "--debug")
        {
          o.debug = true;
        }
        else if (a == "--debug-log")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for " + a);
            exitCode = 2;
            return o;
          }
          const std::string value(*v);
          const bool valid = value == "cache" || value == "graph" || value == "configure" || value == "process" || value == "toolchain" || value == "all";
          if (!valid)
          {
            error("Invalid value for " + a + ": " + value);
            hint("Valid values: cache, graph, configure, process, toolchain, all");
            exitCode = 2;
            return o;
          }
          o.debugLogScope = value;
        }
        else if (a == "--log")
        {
          o.showLog = true;
          if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-')
          {
            const std::string value = args[++i];
            if (value == "build" || value == "configure" || value == "all")
              o.logScope = value;
            else
              o.logPath = value;
          }
        }
        else if (a == "--heartbeat")
        {
          o.heartbeat = true;
        }
        else if (a == "--no-heartbeat")
        {
          o.heartbeat = false;
        }
        else if (a == "--graph-executor")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --graph-executor");
            exitCode = 2;
            return o;
          }
          o.graphExecutor = std::string(*v);
          o.graphExecutorExplicit = true;
          if (o.graphExecutor != "auto" && o.graphExecutor != "on" && o.graphExecutor != "off")
          {
            error("Invalid value for --graph-executor: " + o.graphExecutor);
            hint("Valid values: auto, on, off");
            exitCode = 2;
            return o;
          }
        }
        else if (a.rfind("--graph-executor=", 0) == 0)
        {
          o.graphExecutor = a.substr(std::string("--graph-executor=").size());
          o.graphExecutorExplicit = true;
          if (o.graphExecutor != "auto" && o.graphExecutor != "on" && o.graphExecutor != "off")
          {
            error("Invalid value for --graph-executor: " + o.graphExecutor);
            hint("Valid values: auto, on, off");
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--explain")
        {
          o.explain = true;
        }
        else if (a == "--watch")
        {
          o.watch = true;
        }
        else if (a == "--warnings")
        {
          o.warnings = true;
        }
        else if (a == "--warning-check")
        {
          o.warningCheck = true;
        }
        else if (a == "--sanitize" ||
                 a == "--san")
        {
          if (!select_sanitizer_mode(
                  o,
                  selectedSanitizerMode,
                  selectedSanitizerArgument,
                  process::SanitizerMode::AddressUndefined,
                  a,
                  exitCode))
          {
            return o;
          }
        }
        else if (a == "--asan")
        {
          if (!select_sanitizer_mode(
                  o,
                  selectedSanitizerMode,
                  selectedSanitizerArgument,
                  process::SanitizerMode::Address,
                  a,
                  exitCode))
          {
            return o;
          }
        }
        else if (a == "--ubsan")
        {
          if (!select_sanitizer_mode(
                  o,
                  selectedSanitizerMode,
                  selectedSanitizerArgument,
                  process::SanitizerMode::Undefined,
                  a,
                  exitCode))
          {
            return o;
          }
        }
        else if (a == "--tsan")
        {
          if (!select_sanitizer_mode(
                  o,
                  selectedSanitizerMode,
                  selectedSanitizerArgument,
                  process::SanitizerMode::Thread,
                  a,
                  exitCode))
          {
            return o;
          }
        }
        else if (a.rfind("--sanitize=", 0) == 0)
        {
          const std::string value =
              a.substr(std::string("--sanitize=").size());

          if (value.empty())
          {
            error("Missing value for --sanitize=<mode>");
            hint(
                "Valid modes: address, undefined, "
                "address,undefined, thread");

            exitCode = 2;
            return o;
          }

          const auto parsed =
              vix::engine::parse_sanitizer_mode(value);

          if (!parsed)
          {
            error("Invalid sanitizer mode: " + value);

            hint(
                "Valid modes: address, undefined, "
                "address,undefined, thread");

            hint(
                "Recommended: "
                "vix build --sanitize");

            exitCode = 2;
            return o;
          }

          if (!select_sanitizer_mode(
                  o,
                  selectedSanitizerMode,
                  selectedSanitizerArgument,
                  *parsed,
                  a,
                  exitCode))
          {
            return o;
          }
        }
        else if (a == "--managed-sdk")
        {
          o.managedSdk = true;
        }
        else if (a == "--page")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --page <n>");
            exitCode = 2;
            return o;
          }

          try
          {
            const int page = std::stoi(std::string(*v));
            if (page <= 0)
              throw std::runtime_error("invalid page");

            o.warningsPage = static_cast<std::size_t>(page);
            o.warningsPageSet = true;
          }
          catch (...)
          {
            error("Invalid integer for --page: " + std::string(*v));
            exitCode = 2;
            return o;
          }
        }
        else if (a.rfind("--page=", 0) == 0)
        {
          const std::string v = a.substr(std::string("--page=").size());

          try
          {
            const int page = std::stoi(v);
            if (page <= 0)
              throw std::runtime_error("invalid page");

            o.warningsPage = static_cast<std::size_t>(page);
            o.warningsPageSet = true;
          }
          catch (...)
          {
            error("Invalid integer for --page: " + v);
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--limit")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --limit <n>");
            exitCode = 2;
            return o;
          }

          try
          {
            const int limit = std::stoi(std::string(*v));
            if (limit <= 0)
              throw std::runtime_error("invalid limit");

            o.warningsLimit = static_cast<std::size_t>(limit);
            o.warningsLimitSet = true;
          }
          catch (...)
          {
            error("Invalid integer for --limit: " + std::string(*v));
            exitCode = 2;
            return o;
          }
        }
        else if (a.rfind("--limit=", 0) == 0)
        {
          const std::string v = a.substr(std::string("--limit=").size());

          try
          {
            const int limit = std::stoi(v);
            if (limit <= 0)
              throw std::runtime_error("invalid limit");

            o.warningsLimit = static_cast<std::size_t>(limit);
            o.warningsLimitSet = true;
          }
          catch (...)
          {
            error("Invalid integer for --limit: " + v);
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--preset")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --preset");
            exitCode = 2;
            return o;
          }
          o.preset = std::string(*v);
        }
        else if (a == "--with-sqlite")
        {
          o.withSqlite = true;
        }
        else if (a == "--with-mysql")
        {
          o.withMySql = true;
        }
        else if (!a.empty() && a[0] != '-')
        {
          if (o.singleCpp)
          {
            error("Only one single C++ source file can be passed to vix build.");
            exitCode = 2;
            return o;
          }

          fs::path candidate = fs::path(a);
          if (candidate.extension() == ".cpp" ||
              candidate.extension() == ".cc" ||
              candidate.extension() == ".cxx")
          {
            o.singleCpp = true;
            o.cppFile = fs::absolute(candidate);
          }
          else
          {
            error("Unknown positional argument: " + a);
            hint("For single-file mode, pass a .cpp file.");
            exitCode = 2;
            return o;
          }
        }
        else if (a.rfind("--preset=", 0) == 0)
        {
          o.preset = a.substr(std::string("--preset=").size());
          if (o.preset.empty())
          {
            error("Missing value for --preset");
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--target")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --target <triple>");
            exitCode = 2;
            return o;
          }
          o.targetTriple = std::string(*v);
          if (o.targetTriple == "native")
            o.targetTriple.clear();
        }
        else if (a.rfind("--target=", 0) == 0)
        {
          o.targetTriple = a.substr(std::string("--target=").size());
          if (o.targetTriple.empty())
          {
            error("Missing value for --target <triple>");
            exitCode = 2;
            return o;
          }
          if (o.targetTriple == "native")
            o.targetTriple.clear();
        }
        else if (a == "--build-target")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --build-target <name>");
            exitCode = 2;
            return o;
          }
          o.buildTarget = std::string(*v);
        }
        else if (a.rfind("--build-target=", 0) == 0)
        {
          o.buildTarget = a.substr(std::string("--build-target=").size());
          if (o.buildTarget.empty())
          {
            error("Missing value for --build-target <name>");
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--targets")
        {
          o.listTargets = true;
        }
        else if (a == "--cmake-verbose")
        {
          o.cmakeVerbose = true;
        }
        else if (a == "--sysroot")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --sysroot <path>");
            exitCode = 2;
            return o;
          }
          o.sysroot = std::string(*v);
        }
        else if (a.rfind("--sysroot=", 0) == 0)
        {
          o.sysroot = a.substr(std::string("--sysroot=").size());
        }
        else if (a == "--static")
        {
          o.linkStatic = true;
        }
        else if (a == "-j" || a == "--jobs")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for -j/--jobs");
            exitCode = 2;
            return o;
          }

          try
          {
            o.jobs = std::stoi(std::string(*v));
          }
          catch (...)
          {
            error("Invalid integer for -j/--jobs: " + std::string(*v));
            exitCode = 2;
            return o;
          }
        }
        else if (a.rfind("--jobs=", 0) == 0)
        {
          const std::string v = a.substr(std::string("--jobs=").size());
          try
          {
            o.jobs = std::stoi(v);
          }
          catch (...)
          {
            error("Invalid integer for --jobs: " + v);
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--bin")
        {
          o.exportBin = true;
        }
        else if (a == "--out")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --out <path>");
            exitCode = 2;
            return o;
          }
          o.outPath = std::string(*v);
        }
        else if (a.rfind("--out=", 0) == 0)
        {
          o.outPath = a.substr(std::string("--out=").size());
          if (o.outPath.empty())
          {
            error("Missing value for --out <path>");
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--clean")
        {
          o.clean = true;
        }
        else if (a == "--quiet" || a == "-q")
        {
          o.quiet = true;
        }
        else if (a == "--dir" || a == "-d")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --dir <path>");
            exitCode = 2;
            return o;
          }
          o.dir = std::string(*v);
        }
        else if (a.rfind("--dir=", 0) == 0)
        {
          o.dir = a.substr(std::string("--dir=").size());
          if (o.dir.empty())
          {
            error("Missing value for --dir <path>");
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--fast")
        {
          o.fast = true;
        }
        else if (a == "--report")
        {
          o.report = true;
        }
        else if (a == "--no-cache")
        {
          o.useCache = false;
        }
        else if (a == "--linker")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --linker <auto|default|mold|lld>");
            exitCode = 2;
            return o;
          }

          const auto parsed = util::parse_linker_mode(*v);
          if (!parsed)
          {
            error("Invalid value for --linker: " + std::string(*v));
            hint("Valid: auto, default, mold, lld");
            exitCode = 2;
            return o;
          }
          o.linker = *parsed;
        }
        else if (a.rfind("--linker=", 0) == 0)
        {
          const std::string v = a.substr(std::string("--linker=").size());
          const auto parsed = util::parse_linker_mode(v);
          if (!parsed)
          {
            error("Invalid value for --linker: " + v);
            hint("Valid: auto, default, mold, lld");
            exitCode = 2;
            return o;
          }
          o.linker = *parsed;
        }
        else if (a == "--launcher")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --launcher <auto|none|sccache|ccache>");
            exitCode = 2;
            return o;
          }

          const auto parsed = util::parse_launcher_mode(*v);
          if (!parsed)
          {
            error("Invalid value for --launcher: " + std::string(*v));
            hint("Valid: auto, none, sccache, ccache");
            exitCode = 2;
            return o;
          }
          o.launcher = *parsed;
        }
        else if (a.rfind("--launcher=", 0) == 0)
        {
          const std::string v = a.substr(std::string("--launcher=").size());
          const auto parsed = util::parse_launcher_mode(v);
          if (!parsed)
          {
            error("Invalid value for --launcher: " + v);
            hint("Valid: auto, none, sccache, ccache");
            exitCode = 2;
            return o;
          }
          o.launcher = *parsed;
        }
        else if (a == "--no-status")
        {
          o.status = false;
        }
        else if (a == "--no-up-to-date")
        {
          o.dryUpToDate = false;
        }
        else
        {
          error("Unknown argument: " + a);
          hint("Run: vix build --help");
          exitCode = 2;
          return o;
        }
      }

      if ((o.warningsPageSet || o.warningsLimitSet) && !o.warnings)
      {
        error("--page and --limit can only be used with --warnings");
        hint("Try: vix build --warnings --page 2 --limit 10");
        exitCode = 2;
        return o;
      }

      return o;
    }

    static std::optional<std::string> detect_launcher(const process::Options &opt)
    {
      return vix::engine::detect_compiler_launcher(opt.launcher);
    }

    static std::optional<std::string> detect_fast_linker_flag(const process::Options &opt)
    {
      return vix::engine::detect_fast_linker_flag(opt.linker);
    }

    static void clean_local_build_dir(
        const fs::path &buildDir,
        bool quiet)
    {
      if (!fs::exists(buildDir))
        return;

      std::error_code ec;
      fs::remove_all(buildDir, ec);

      if (ec)
      {
        error(
            "Failed to remove build directory: " +
            buildDir.string());

        hint(ec.message());
        throw std::runtime_error("clean failed");
      }

      if (!quiet)
        step("removed " + buildDir.string());
    }

    static std::vector<std::pair<std::string, std::string>>
    build_cmake_vars(
        const process::Preset &p,
        const process::Options &opt,
        const fs::path &toolchainFile,
        const std::optional<std::string> &launcher,
        const std::optional<std::string> &fastLinkerFlag,
        const fs::path &globalPackagesFile,
        vix::engine::DependencyEnvironmentMode dependencyEnvironmentMode,
        const fs::path &sdkConfigDir)
    {
      vix::engine::CMakeConfigurationOptions engineOptions;
      engineOptions.buildType = p.buildType;
      engineOptions.targetTriple = opt.targetTriple;
      engineOptions.linkStatic = opt.linkStatic;
      engineOptions.withSqlite = opt.withSqlite;
      engineOptions.withMySql = opt.withMySql;
      engineOptions.warningCheck = opt.warningCheck;
      engineOptions.toolchainFile = toolchainFile;
      engineOptions.globalPackagesFile = globalPackagesFile;
      engineOptions.dependencyEnvironmentMode =
          dependencyEnvironmentMode;
      engineOptions.sdkConfigDir = sdkConfigDir;
      engineOptions.launcher = launcher;
      engineOptions.fastLinkerFlag = fastLinkerFlag;

      auto variables =
          vix::engine::make_cmake_variables(engineOptions);

      if (vix::engine::sanitizer_enabled(opt.sanitizerMode))
      {
        variables.emplace_back(
            "VIX_SANITIZER_MODE",
            std::string(
                vix::engine::sanitizer_mode_name(
                    opt.sanitizerMode)));
      }

      return variables;
    }

    static vix::engine::ConfigurationSignatureOptions
    configuration_signature_options(
        const process::Options &opt,
        const std::string &toolchainContent)
    {
      vix::engine::ConfigurationSignatureOptions options;
      options.linkStatic = opt.linkStatic;
      options.targetTriple = opt.targetTriple;
      options.sysroot = opt.sysroot;
      // --fast changes only the build execution strategy. It must not
      // invalidate a CMake configuration produced without that flag.
      options.useCache = opt.useCache;
      options.warningCheck = opt.warningCheck;
      options.linker = opt.linker;
      options.launcher = opt.launcher;
      options.verbose = opt.verbose;
      options.cmakeVerbose = opt.cmakeVerbose;
      options.rawCMakeArgs = opt.cmakeArgs;
      options.toolchainContent = toolchainContent;
      return options;
    }

    static std::string build_configuration_signature(
        const process::Plan &plan,
        const process::Options &opt,
        const std::string &toolchainContent)
    {
      return vix::engine::make_configuration_signature(
          plan,
          configuration_signature_options(opt, toolchainContent));
    }

    static std::uint64_t fast_file_size_or_zero(const fs::path &path)
    {
      std::error_code ec;
      const auto size = fs::file_size(path, ec);
      return ec ? 0 : static_cast<std::uint64_t>(size);
    }

    static std::uint64_t fast_file_mtime_count(const fs::path &path)
    {
      std::error_code ec;
      const auto t = fs::last_write_time(path, ec);
      if (ec)
        return 0;

      return static_cast<std::uint64_t>(t.time_since_epoch().count());
    }

    static std::string first_changed_project_input(
        const fs::path &projectDir,
        const std::vector<artifact_cache::ProjectInput> &inputs)
    {
      const fs::path root = fs::absolute(projectDir).lexically_normal();

      for (const auto &input : inputs)
      {
        const fs::path path = root / fs::path(input.path);
        std::error_code ec;

        if (!fs::exists(path, ec) || ec)
          return input.path + " (missing)";

        if (!fs::is_regular_file(path, ec) || ec)
          return input.path + " (not a regular file)";

        if (fast_file_size_or_zero(path) != input.size)
          return input.path + " (size changed)";

        if (fast_file_mtime_count(path) != input.mtime)
          return input.path + " (mtime changed)";
      }

      return {};
    }

    static bool previous_project_inputs_still_current(
        const fs::path &projectDir,
        const std::vector<artifact_cache::ProjectInput> &inputs)
    {
      return first_changed_project_input(projectDir, inputs).empty();
    }

    static bool cmake_globs_still_current(const fs::path &buildDir)
    {
      const fs::path verify = buildDir / "CMakeFiles" / "VerifyGlobs.cmake";
      const fs::path stamp = buildDir / "CMakeFiles" / "cmake.verify_globs";

      if (!util::file_exists(verify))
        return true;

      const std::uint64_t before = util::file_exists(stamp)
                                       ? fast_file_mtime_count(stamp)
                                       : 0;

      std::string out;
      const process::ExecResult result = build::run_process_capture(
          {"cmake", "-P", verify.string()},
          {},
          out);

      if (result.exitCode != 0)
        return false;

      const std::uint64_t after = util::file_exists(stamp)
                                      ? fast_file_mtime_count(stamp)
                                      : 0;

      return before == after;
    }

    static std::optional<ResolvedBuildPlan> make_plan(
        const process::Options &opt,
        const fs::path &cwd)
    {
      fs::path base = cwd;

      if (!opt.dir.empty())
        base = fs::absolute(fs::path(opt.dir));

      fs::path userProjectDir;
      const auto root = util::find_project_root(base);

      if (root)
      {
        userProjectDir = *root;
      }
      else if (util::file_exists(base / "CMakeLists.txt") ||
               util::file_exists(base / "vix.app"))
      {
        userProjectDir = base;
      }
      else
      {
        return std::nullopt;
      }

      userProjectDir = fs::absolute(userProjectDir).lexically_normal();

      fs::path cmakeSourceDir = userProjectDir;
      std::string defaultTargetName = userProjectDir.filename().string();
      bool generatedFromVixApp = false;

      const fs::path cmakeListsPath = userProjectDir / "CMakeLists.txt";
      const fs::path appManifestPath = userProjectDir / "vix.app";

      const bool hasCMakeLists = util::file_exists(cmakeListsPath);
      const bool hasVixApp = util::file_exists(appManifestPath);

      if (!hasCMakeLists && hasVixApp)
      {
        const app::AppProjectResolveResult resolved =
            app::resolve_app_project(userProjectDir);

        if (!resolved.success())
        {
          error("Failed to resolve vix.app project.");
          hint(resolved.error);
          return std::nullopt;
        }

        cmakeSourceDir = resolved.cmakeSourceDir;
        defaultTargetName = resolved.targetName;
        generatedFromVixApp = true;
      }

      const auto presetOpt = build::resolve_builtin_preset(opt.preset);

      if (!presetOpt)
        return std::nullopt;

      vix::engine::ExecutionPlanLayoutOptions layoutOptions;
      layoutOptions.userProjectDir = userProjectDir;
      layoutOptions.cmakeSourceDir = cmakeSourceDir;
      layoutOptions.defaultTargetName = defaultTargetName;
      layoutOptions.generatedFromVixApp = generatedFromVixApp;
      layoutOptions.preset = *presetOpt;
      layoutOptions.targetTriple = opt.targetTriple;
      layoutOptions.sanitizerMode = opt.sanitizerMode;

      const vix::engine::ExecutionPlanLayoutResult layout =
          vix::engine::make_execution_plan_layout(layoutOptions);

      if (!layout.success())
        return std::nullopt;

      process::Plan plan = layout.plan;

      plan.launcher = detect_launcher(opt);
      plan.fastLinkerFlag = detect_fast_linker_flag(opt);
      plan.projectFingerprint =
          util::compute_cmake_config_fingerprint(plan.cmakeSourceDir);

      if (opt.clean)
      {
        try
        {
          clean_local_build_dir(
              plan.buildDir,
              opt.quiet);
        }
        catch (const std::exception &)
        {
          return std::nullopt;
        }
      }

      const fs::path globalPackagesFile = plan.buildDir / "vix-global-packages.cmake";

      std::string sdkResolutionError;
      resolve_sdk_for_plan(plan, sdkResolutionError, opt);

      std::string toolchainContent;

      if (!opt.targetTriple.empty())
      {
        toolchainContent =
            build::toolchain_contents_for_triple(opt.targetTriple, opt.sysroot);
      }

      plan.cmakeVars = build_cmake_vars(
          plan.preset,
          opt,
          plan.toolchainFile,
          plan.launcher,
          plan.fastLinkerFlag,
          globalPackagesFile,
          plan.dependencyEnvironmentMode,
          plan.sdkConfigDir);

      plan.signature = build_configuration_signature(plan, opt, toolchainContent);

      return ResolvedBuildPlan{
          std::move(plan),
          std::move(sdkResolutionError)};
    }
    static vix::engine::ConfigureDecision
    evaluate_configure_decision(
        const process::Options &opt,
        const process::Plan &plan)
    {
      vix::engine::ConfigureDecisionOptions options;
      options.useCache = opt.useCache;
      options.clean = opt.clean;
      options.buildDir = plan.buildDir;
      options.signatureFile = plan.sigFile;
      options.expectedSignature = plan.signature;
      return vix::engine::evaluate_configuration(options);
    }

    static std::string platform_executable_name(const std::string &name)
    {
#ifdef _WIN32
      if (name.size() >= 4 && name.substr(name.size() - 4) == ".exe")
        return name;
      return name + ".exe";
#else
      return name;
#endif
    }

    static bool is_executable_candidate(const fs::path &p)
    {
      std::error_code ec{};

      if (!fs::is_regular_file(p, ec) || ec)
        return false;

#ifdef _WIN32
      return p.extension() == ".exe";
#else
      const auto perms = fs::status(p, ec).permissions();
      if (ec)
        return false;

      using pr = fs::perms;
      return (perms & pr::owner_exec) != pr::none ||
             (perms & pr::group_exec) != pr::none ||
             (perms & pr::others_exec) != pr::none;
#endif
    }

    static bool looks_like_test_binary(const fs::path &p)
    {
      const std::string n = p.filename().string();
      return n.find("_test") != std::string::npos ||
             n.find("_tests") != std::string::npos ||
             n.rfind("test_", 0) == 0;
    }

    static std::optional<fs::path> resolve_main_executable(
        const fs::path &buildDir,
        const fs::path &projectDir,
        const std::string &buildTarget,
        const std::string &defaultTargetName)
    {
      const std::string preferredBase =
          !buildTarget.empty()
              ? buildTarget
              : (!defaultTargetName.empty()
                     ? defaultTargetName
                     : projectDir.filename().string());

      const std::string preferredName = platform_executable_name(preferredBase);

      const std::vector<fs::path> preferredPaths = {
          buildDir / preferredName,
          buildDir / "bin" / preferredName,
          buildDir / "src" / preferredName};

      for (const auto &p : preferredPaths)
      {
        if (is_executable_candidate(p) && !looks_like_test_binary(p))
          return p;
      }

      std::vector<fs::path> candidates;
      std::error_code ec{};

      for (auto it = fs::recursive_directory_iterator(
               buildDir,
               fs::directory_options::skip_permission_denied,
               ec);
           !ec && it != fs::recursive_directory_iterator();
           ++it)
      {
        const fs::path p = it->path();

        if (p.string().find("CMakeFiles") != std::string::npos)
          continue;

        if (!is_executable_candidate(p))
          continue;

        if (looks_like_test_binary(p))
          continue;

#ifdef _WIN32
        const std::string baseName = p.stem().string();
#else
        const std::string baseName = p.filename().string();
#endif

        if (baseName == preferredBase || p.filename().string() == preferredName)
          return p;

        candidates.push_back(p);
      }

      if (candidates.size() == 1)
        return candidates.front();

      return std::nullopt;
    }

    static fs::path vix_home_dir()
    {
#ifdef _WIN32
      const char *home = std::getenv("USERPROFILE");
#else
      const char *home = std::getenv("HOME");
#endif

      if (home && *home)
      {
        return fs::path(home) / ".vix";
      }

      return fs::current_path() / ".vix";
    }

    static std::string json_escape_string(const std::string &value)
    {
      std::string out;
      out.reserve(value.size());

      for (char c : value)
      {
        switch (c)
        {
        case '\\':
          out += "\\\\";
          break;
        case '"':
          out += "\\\"";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        default:
          out += c;
          break;
        }
      }

      return out;
    }

    static void write_project_build_metadata(
        const fs::path &projectDir,
        const fs::path &buildDir,
        const fs::path &binary,
        vix::engine::SanitizerMode sanitizerMode)
    {
      if (projectDir.empty() || binary.empty())
        return;

      const fs::path metaDir =
          projectDir / ".vix";

      const fs::path metaFile =
          metaDir / "meta.json";

      std::error_code ec;
      fs::create_directories(metaDir, ec);

      if (ec)
        return;

      fs::path absoluteBinary =
          fs::absolute(binary, ec);

      if (ec)
      {
        ec.clear();
        absoluteBinary = binary;
      }

      absoluteBinary =
          absoluteBinary.lexically_normal();

      fs::path absoluteBuildDir =
          fs::absolute(buildDir, ec);

      if (ec)
      {
        ec.clear();
        absoluteBuildDir = buildDir;
      }

      absoluteBuildDir =
          absoluteBuildDir.lexically_normal();

      std::ostringstream out;

      out << "{\n";

      out << "  \"last_binary\": \""
          << json_escape_string(absoluteBinary.string())
          << "\",\n";

      out << "  \"build_dir\": \""
          << json_escape_string(absoluteBuildDir.string())
          << "\",\n";

      out << "  \"sanitizer\": \""
          << vix::engine::sanitizer_mode_name(sanitizerMode)
          << "\"\n";

      out << "}\n";

      (void)write_if_different(
          metaFile,
          out.str());
    }

    static void write_last_binary(const fs::path &path)
    {
      const fs::path metaDir = vix_home_dir();
      const fs::path metaFile = metaDir / "meta.json";

      std::error_code ec;
      fs::create_directories(metaDir, ec);

      if (ec)
        return;

      std::ofstream out(metaFile);
      if (!out)
        return;

      out << "{\n";
      out << "  \"last_binary\": \""
          << json_escape_string(fs::absolute(path).string())
          << "\"\n";
      out << "}\n";
    }

    static bool sdk_scan_skip_dir(const fs::path &path)
    {
      for (const auto &part : path)
      {
        const std::string item = part.string();
        if (item == ".git" || item == "build" || item == "build-ninja" ||
            item == "build-release" || item == "CMakeFiles" || item == "_deps" ||
            item == "deps" || item == "node_modules")
          return true;
      }
      return false;
    }

    static bool sdk_scan_file_name(const fs::path &path)
    {
      const std::string name = path.filename().string();
      return name == "CMakeLists.txt" || name == "vix.app" ||
             name == "vix.module" || path.extension() == ".cmake";
    }

    static void collect_vix_targets_from_text(const std::string &text, std::set<std::string> &targets)
    {
      const std::string prefix = "vix::";
      std::size_t pos = 0;
      while ((pos = text.find(prefix, pos)) != std::string::npos)
      {
        std::size_t end = pos + prefix.size();
        while (end < text.size())
        {
          const unsigned char c = static_cast<unsigned char>(text[end]);
          if (!(std::isalnum(c) || text[end] == '_'))
            break;
          ++end;
        }
        if (end > pos + prefix.size())
          targets.insert(text.substr(pos, end - pos));
        pos = end;
      }
    }

    static bool sdk_debug_enabled(const process::Options &opt)
    {
      if (opt.debug || !opt.debugLogScope.empty())
        return true;
      const char *level = std::getenv("VIX_LOG_LEVEL");
      if (!level)
        return false;

      std::string value(level);
      std::transform(
          value.begin(),
          value.end(),
          value.begin(),
          [](unsigned char c)
          {
            return static_cast<char>(std::tolower(c));
          });

      return value == "debug" || value == "trace";
    }

    static bool cmake_argument_sets_variable(
        const std::string &argument,
        const std::string &name)
    {
      const std::string prefix = "-D" + name;

      if (argument.rfind(prefix, 0) != 0)
        return false;

      if (argument.size() <= prefix.size())
        return false;

      const char next = argument[prefix.size()];
      return next == '=' || next == ':';
    }

    static std::optional<std::string> cmake_argument_variable_value(
        const std::string &argument,
        const std::string &name)
    {
      if (!cmake_argument_sets_variable(argument, name))
        return std::nullopt;

      const std::size_t eq = argument.find('=');
      if (eq == std::string::npos)
        return std::nullopt;

      return argument.substr(eq + 1);
    }

    static bool vix_config_file_exists(const fs::path &dir)
    {
      return util::file_exists(dir / "VixConfig.cmake") ||
             util::file_exists(dir / "vixConfig.cmake");
    }

    static bool valid_vix_config_dir_hint(const std::string &raw)
    {
      if (raw.empty())
        return false;

      const fs::path path(raw);

      if (util::file_exists(path))
      {
        const std::string name = path.filename().string();
        return name == "VixConfig.cmake" ||
               name == "vixConfig.cmake";
      }

      return vix_config_file_exists(path);
    }

    static std::vector<std::string> split_cmake_path_list(
        const std::string &value)
    {
      std::vector<std::string> out;
      std::string current;

      auto flush =
          [&]()
      {
        if (!current.empty())
          out.push_back(current);
        current.clear();
      };

      for (char c : value)
      {
#ifdef _WIN32
        const bool sep = c == ';';
#else
        const bool sep = c == ';' || c == ':';
#endif
        if (sep)
          flush();
        else
          current.push_back(c);
      }

      flush();
      return out;
    }

    static bool valid_vix_prefix_hint(const std::string &raw)
    {
      for (const std::string &entry : split_cmake_path_list(raw))
      {
        if (entry.empty())
          continue;

        const fs::path prefix(entry);

        if (vix_config_file_exists(prefix / "lib" / "cmake" / "Vix") ||
            vix_config_file_exists(prefix / "share" / "cmake" / "Vix") ||
            vix_config_file_exists(prefix / "share" / "Vix" / "cmake") ||
            vix_config_file_exists(prefix))
        {
          return true;
        }
      }

      return false;
    }

    static bool valid_existing_file_hint(const std::string &raw)
    {
      return !raw.empty() && util::file_exists(fs::path(raw));
    }

    static bool has_explicit_vix_discovery_override(
        const process::Options &opt)
    {
      const char *configDirEnvironmentVariables[] = {
          "Vix_DIR",
          "vix_DIR"};

      for (const char *name : configDirEnvironmentVariables)
      {
        const char *value = std::getenv(name);
        if (value && valid_vix_config_dir_hint(value))
          return true;
      }

      const char *prefixEnvironmentVariables[] = {
          "Vix_ROOT",
          "vix_ROOT",
          "CMAKE_PREFIX_PATH",
          "CMAKE_FIND_ROOT_PATH"};

      for (const char *name : prefixEnvironmentVariables)
      {
        const char *value = std::getenv(name);
        if (value && valid_vix_prefix_hint(value))
          return true;
      }

      if (const char *value = std::getenv("CMAKE_TOOLCHAIN_FILE"))
      {
        if (valid_existing_file_hint(value))
          return true;
      }

      const char *configDirCmakeVariables[] = {
          "Vix_DIR",
          "vix_DIR"};

      const char *prefixCmakeVariables[] = {
          "Vix_ROOT",
          "vix_ROOT",
          "CMAKE_PREFIX_PATH",
          "CMAKE_FIND_ROOT_PATH"};

      for (const std::string &argument :
           opt.cmakeArgs)
      {
        for (const char *name : configDirCmakeVariables)
        {
          const auto value =
              cmake_argument_variable_value(argument, name);
          if (value && valid_vix_config_dir_hint(*value))
            return true;
        }

        for (const char *name : prefixCmakeVariables)
        {
          const auto value =
              cmake_argument_variable_value(argument, name);
          if (value && valid_vix_prefix_hint(*value))
            return true;
        }

        if (const auto value =
                cmake_argument_variable_value(
                    argument,
                    "CMAKE_TOOLCHAIN_FILE"))
        {
          if (valid_existing_file_hint(*value))
            return true;
        }
      }

      return false;
    }

    static bool has_local_vix_dependency_modules(
        const fs::path &projectDir,
        const std::set<std::string> &modules)
    {
      if (modules.empty())
        return false;

      const fs::path root =
          projectDir / "deps" / "vix";

      for (const std::string &module : modules)
      {
        if (!util::file_exists(root / module / "CMakeLists.txt"))
          return false;
      }

      return true;
    }

    static std::set<std::string> collect_project_vix_targets(const fs::path &projectDir, const fs::path &cmakeSourceDir)
    {
      std::set<std::string> targets;
      std::vector<fs::path> roots{projectDir};
      if (!cmakeSourceDir.empty() && cmakeSourceDir != projectDir)
        roots.push_back(cmakeSourceDir);

      for (const fs::path &root : roots)
      {
        std::error_code ec;
        if (!fs::exists(root, ec) || ec)
          continue;

        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); ++it)
        {
          const fs::path path = it->path();
          if (it->is_directory(ec))
          {
            if (sdk_scan_skip_dir(path.lexically_relative(root)))
              it.disable_recursion_pending();
            continue;
          }

          if (!it->is_regular_file(ec) || !sdk_scan_file_name(path))
            continue;

          collect_vix_targets_from_text(util::read_text_file_or_empty(path), targets);
        }
      }

      return targets;
    }

    static std::string join_words(const std::vector<std::string> &items)
    {
      std::string out;
      for (const std::string &item : items)
      {
        if (!out.empty())
          out += " ";
        out += item;
      }
      return out;
    }

    static std::string sdk_resolution_error_message(
        const vix::cli::sdk::SdkResolution &resolution)
    {
      std::ostringstream out;

      if (!resolution.missingModules.empty())
      {
        out << "Missing SDK modules\n";
        for (const std::string &module : resolution.missingModules)
        {
          const std::string target =
              vix::cli::sdk::target_for_module(module);
          std::string provider;
          const auto providerIt =
              resolution.moduleProviders.find(module);
          if (providerIt != resolution.moduleProviders.end())
            provider = providerIt->second;
          else if (const auto fallback =
                       vix::cli::sdk::provider_profile_for_module(module))
            provider = *fallback;

          out << target;
          if (!provider.empty())
            out << "  provider profile: " << provider;
          out << "\n";
        }

        std::set<std::string> providers;
        for (const std::string &module : resolution.missingModules)
        {
          const auto providerIt =
              resolution.moduleProviders.find(module);
          if (providerIt != resolution.moduleProviders.end())
            providers.insert(providerIt->second);
        }

        for (const std::string &provider : providers)
          out << "run: vix upgrade --sdk " << provider << "\n";
      }

      if (!resolution.error.empty())
      {
        if (out.tellp() > 0)
          out << "\n";
        out << "Managed Vix SDK profiles error: "
            << resolution.error
            << "\n";
      }

      return out.str();
    }

    static void resolve_sdk_for_plan(
        process::Plan &plan,
        std::string &sdkResolutionError,
        const process::Options &opt)
    {
      plan.sdkConfigDir.clear();
      sdkResolutionError.clear();
      plan.dependencyEnvironmentMode =
          vix::engine::DependencyEnvironmentMode::Native;

      const char *managedSdkEnv = std::getenv("VIX_BUILD_MANAGED_SDK");
      const bool envManagedSdk =
          managedSdkEnv &&
          *managedSdkEnv &&
          std::string(managedSdkEnv) != "0" &&
          std::string(managedSdkEnv) != "false" &&
          std::string(managedSdkEnv) != "FALSE";

      if (!opt.managedSdk && !envManagedSdk)
      {
        if (sdk_debug_enabled(opt) && !opt.quiet)
        {
          hint("SDK resolution:");
          hint("  route: native CMake discovery");
        }
        return;
      }

      plan.dependencyEnvironmentMode =
          vix::engine::DependencyEnvironmentMode::ManagedSdk;

      /*
       * Explicit CMake discovery always has priority.
       *
       * Vix must not override explicit Vix package hints,
       * custom package roots or package-manager toolchains.
       */
      if (has_explicit_vix_discovery_override(opt))
      {
        if (sdk_debug_enabled(opt) && !opt.quiet)
        {
          hint("SDK resolution:");
          hint("  route: explicit CMake discovery override");
        }
        return;
      }

      const std::set<std::string> targets =
          collect_project_vix_targets(
              plan.userProjectDir,
              plan.cmakeSourceDir);

      const std::set<std::string> modules =
          vix::cli::sdk::normalize_required_vix_targets(
              targets);

      if (modules.empty())
        return;

      if (has_local_vix_dependency_modules(
              plan.userProjectDir,
              modules))
      {
        if (sdk_debug_enabled(opt) && !opt.quiet)
        {
          hint("SDK resolution:");
          hint("  route: local deps/vix source modules");
          hint("  required modules: " +
               join_words(
                   std::vector<std::string>(
                       modules.begin(),
                       modules.end())));
        }

        return;
      }

      /*
       * Managed SDK profiles are optional.
       *
       * They are selected only when a complete and coherent
       * profile set is already installed. Otherwise, normal
       * CMake package discovery remains responsible for
       * locating Vix.
       */
      const vix::cli::sdk::SdkResolution resolution =
          vix::cli::sdk::resolve_profiles_for_modules(
              modules);

      if (sdk_debug_enabled(opt) && !opt.quiet)
      {
        hint("SDK resolution:");
        hint("  route: managed profile candidate");
        hint("  required modules: " +
             join_words(
                 std::vector<std::string>(
                     modules.begin(),
                     modules.end())));
        hint("  selected profiles: " +
             (resolution.selectedProfiles.empty()
                  ? std::string("none")
                  : join_words(resolution.selectedProfiles)));
        if (!resolution.missingModules.empty())
        {
          hint("  missing modules: " +
               join_words(resolution.missingModules));
        }
      }

      if (!resolution.ok ||
          resolution.selectedProfiles.empty())
      {
        if (!resolution.ok)
        {
          sdkResolutionError =
              sdk_resolution_error_message(
                  resolution);
          return;
        }

        if (opt.verbose)
        {
          if (!resolution.error.empty())
          {
            hint(
                "Managed Vix SDK profiles are unavailable: " +
                resolution.error);
          }

          hint(
              "Falling back to normal CMake package discovery.");
        }

        return;
      }

      const std::string identity =
          resolution.version +
          "-" +
          join_words(resolution.selectedProfiles);

      const fs::path composedRoot =
          plan.buildDir /
          ".vix-sdk" /
          identity;

      std::string sdkError;

      if (!vix::cli::sdk::write_composed_sdk_config(
              composedRoot,
              resolution,
              sdkError))
      {
        if (opt.verbose)
        {
          hint(
              "Unable to compose managed Vix SDK profiles.");

          if (!sdkError.empty())
            hint(sdkError);

          hint(
              "Falling back to normal CMake package discovery.");
        }

        return;
      }

      plan.sdkConfigDir =
          composedRoot /
          "lib" /
          "cmake" /
          "Vix";
    }
    static bool export_built_binary(
        const fs::path &sourceExe,
        const fs::path &destination,
        bool quiet)
    {
      std::error_code ec{};

      fs::path finalDest = destination;

      if (fs::exists(destination, ec) && fs::is_directory(destination, ec))
        finalDest = destination / sourceExe.filename();

      const fs::path parent = finalDest.parent_path();
      if (!parent.empty())
      {
        fs::create_directories(parent, ec);
        if (ec)
        {
          error("Failed to create output directory: " + parent.string());
          hint(ec.message());
          return false;
        }
      }

      fs::copy_file(sourceExe, finalDest, fs::copy_options::overwrite_existing, ec);
      if (ec)
      {
        error("Failed to export binary to: " + finalDest.string());
        hint(ec.message());
        return false;
      }

#ifndef _WIN32
      const auto perms = fs::status(sourceExe, ec).permissions();
      if (!ec)
        fs::permissions(finalDest, perms, fs::perm_options::replace, ec);
#endif

      if (!quiet)
        success("Exported binary: " + finalDest.string());

      write_last_binary(finalDest);

      return true;
    }

    static bool can_use_graph_build(
        const process::Options &opt,
        const process::Plan &plan,
        const build::BuildGraphScanResult &scan)
    {
      (void)opt;
      (void)plan;
      (void)scan;

      return false;
    }

    static fs::path graph_output_binary_path(
        const process::Options &opt,
        const process::Plan &plan)
    {
      if (!opt.outPath.empty())
        return fs::absolute(fs::path(opt.outPath));

      const std::string target =
          !plan.defaultTargetName.empty()
              ? plan.defaultTargetName
              : plan.projectDir.filename().string();

      return plan.buildDir / platform_executable_name(target);
    }

    static bool file_exists_regular(const fs::path &path)
    {
      std::error_code ec;
      return fs::exists(path, ec) && fs::is_regular_file(path, ec);
    }

    static bool collect_compile_task_paths(
        const build::BuildGraph &graph,
        const build::BuildTask &task,
        fs::path &sourcePath,
        fs::path &objectPath,
        std::vector<fs::path> &dependencyPaths)
    {
      sourcePath.clear();
      objectPath.clear();
      dependencyPaths.clear();

      for (const auto &inputId : task.inputs)
      {
        const build::BuildNode *node = graph.find_node(inputId);
        if (!node)
          continue;

        if (node->kind == build::BuildNodeKind::Source && sourcePath.empty())
        {
          sourcePath = node->path;
          continue;
        }

        if (node->kind == build::BuildNodeKind::Header ||
            node->kind == build::BuildNodeKind::Config)
        {
          dependencyPaths.push_back(node->path);
        }
      }

      for (const auto &outputId : task.outputs)
      {
        const build::BuildNode *node = graph.find_node(outputId);
        if (!node)
          continue;

        if (node->kind == build::BuildNodeKind::Object)
        {
          objectPath = node->path;
          break;
        }
      }

      return !sourcePath.empty() && !objectPath.empty();
    }

    static std::string compile_task_source_subject(
        const build::BuildGraph &graph,
        const build::BuildTask &task,
        const fs::path &projectDir)
    {
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
        return task.id;
      }

      return watch_relative_path(sourcePath, projectDir);
    }

    static std::string compile_task_subject_for_id(
        const build::BuildGraph &graph,
        const std::string &taskId,
        const fs::path &projectDir)
    {
      const build::BuildTask *task = graph.find_task(taskId);

      if (!task)
        return taskId;

      return compile_task_source_subject(graph, *task, projectDir);
    }

    static std::string compile_task_summary_subject(
        const build::BuildGraph &graph,
        const std::vector<std::string> &taskIds,
        const fs::path &projectDir)
    {
      std::set<std::string> sourceSubjects;

      for (const std::string &taskId : taskIds)
      {
        const build::BuildTask *task = graph.find_task(taskId);

        if (!task)
          continue;

        sourceSubjects.insert(
            compile_task_source_subject(
                graph,
                *task,
                projectDir));
      }

      if (sourceSubjects.size() == 1)
        return *sourceSubjects.begin();

      if (!sourceSubjects.empty())
        return std::to_string(sourceSubjects.size()) + " files";

      if (taskIds.size() == 1)
        return taskIds.front();

      return std::to_string(taskIds.size()) + " files";
    }

    static build::BuildTaskResult run_cached_graph_compile_task(
        const build::BuildGraph &graph,
        const build::ObjectCache &objectCache,
        build::BuildTask &task)
    {
      build::BuildTaskResult result;
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
        result.state = build::BuildTaskState::Failed;
        result.exitCode = 127;
        result.output = "Invalid graph compile task: " + task.id + "\n";
        return result;
      }

      const fs::path dependencyFilePath =
          build::dependency_file_for_object(objectPath);

      const build::ObjectCacheResult restored =
          objectCache.resolve_compile_task(
              task,
              sourcePath,
              dependencyPaths,
              objectPath,
              dependencyFilePath,
              graph.config().buildFingerprint);

      if (restored.hit)
      {
        result.state = build::BuildTaskState::Skipped;
        result.exitCode = 0;
        result.output = "cache hit: " + sourcePath.string() + "\n";
        return result;
      }

      result = build::execute_build_task_process(task);

      if (result.exitCode != 0)
        return result;

      const std::string inputHash =
          build::ObjectCache::compute_input_hash(sourcePath, dependencyPaths);

      const std::string objectKey =
          build::ObjectCache::compute_object_key(
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

    static bool debug_build_details_enabled(const process::Options &opt)
    {
      if (opt.debug || !opt.debugLogScope.empty())
        return true;
      const char *level = std::getenv("VIX_LOG_LEVEL");

      if (!level || !*level)
        return false;

      std::string value(level);

      for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      return value == "debug" || value == "trace";
    }

    static void print_debug_command_if_enabled(
        const process::Options &opt,
        const process::ExecResult &result)
    {
      if (!debug_build_details_enabled(opt))
        return;

      if (result.displayCommand.empty())
        return;

      std::cerr << GRAY
                << "command: "
                << RESET
                << result.displayCommand
                << "\n";
    }

    static bool looks_like_compiler_warning(const std::string &line)
    {
      return line.find(": warning:") != std::string::npos ||
             line.find(" warning: ") != std::string::npos;
    }

    static std::vector<std::string> collect_compiler_warnings(
        const std::string &output,
        std::size_t maxWarnings = 5)
    {
      std::istringstream in(output);
      std::string line;

      std::vector<std::string> warnings;

      while (std::getline(in, line))
      {
        if (!looks_like_compiler_warning(line))
          continue;

        warnings.push_back(line);

        if (warnings.size() >= maxWarnings)
          break;
      }

      return warnings;
    }

    static std::size_t count_compiler_warnings(const std::string &output)
    {
      std::istringstream in(output);
      std::string line;

      std::size_t count = 0;

      while (std::getline(in, line))
      {
        if (looks_like_compiler_warning(line))
          ++count;
      }

      return count;
    }

    static void print_compiler_warnings_summary(const std::string &output)
    {
      const std::size_t total = count_compiler_warnings(output);

      if (total == 0)
        return;

      const std::vector<std::string> warningLines =
          collect_compiler_warnings(output, 5);

      std::vector<build::BuildWarning> warnings;
      warnings.reserve(warningLines.size());

      for (const std::string &line : warningLines)
      {
        const auto parsed = build::parse_build_warning(line);

        if (parsed)
        {
          warnings.push_back(*parsed);
          continue;
        }

        build::BuildWarning warning;
        warning.raw = line;
        warnings.push_back(warning);
      }

      build::print_build_warnings_summary(
          std::cout,
          warnings,
          total);
    }

    static std::vector<build::BuildWarning> collect_all_build_warnings(
        const std::string &output)
    {
      std::istringstream in(output);
      std::string line;

      std::vector<build::BuildWarning> warnings;

      while (std::getline(in, line))
      {
        if (!looks_like_compiler_warning(line))
          continue;

        const auto parsed = build::parse_build_warning(line);

        if (parsed)
        {
          warnings.push_back(*parsed);
          continue;
        }

        build::BuildWarning warning;
        warning.raw = line;
        warnings.push_back(warning);
      }

      return warnings;
    }

    static std::string display_build_profile(const process::Plan &plan)
    {
      if (plan.preset.buildType == "Release")
        return "release";

      return "dev";
    }

    static int print_last_build_warnings(
        const process::Options &opt,
        const process::Plan &plan)
    {
      const std::string log =
          util::read_text_file_or_empty(plan.buildLog);

      if (log.empty())
      {
        error("No build log found.");
        hint("Run `vix build` first.");
        return 1;
      }

      const std::vector<build::BuildWarning> warnings =
          collect_all_build_warnings(log);

      if (warnings.empty())
      {
        build::print_task_header_full(
            std::cout,
            "Warnings",
            build::default_build_target_name(opt, plan),
            display_build_profile(plan),
            {});

        build::print_build_success(
            std::cout,
            "No compiler warnings found");

        return 0;
      }

      build::print_task_header_full(
          std::cout,
          "Warnings",
          build::default_build_target_name(opt, plan),
          display_build_profile(plan),
          {});

      const std::size_t total = warnings.size();
      const std::size_t limit = opt.warningsLimit == 0 ? 10 : opt.warningsLimit;
      const std::size_t page = opt.warningsPage == 0 ? 1 : opt.warningsPage;

      const std::size_t start = (page - 1) * limit;

      if (start >= total)
      {
        error("Warnings page is out of range.");
        hint("Total warnings: " + std::to_string(total));
        hint("Last page: " + std::to_string((total + limit - 1) / limit));
        return 1;
      }

      const std::size_t end = std::min(start + limit, total);

      std::vector<build::BuildWarning> pageWarnings;
      pageWarnings.reserve(end - start);

      for (std::size_t i = start; i < end; ++i)
        pageWarnings.push_back(warnings[i]);

      build::print_build_warnings_summary(
          std::cout,
          pageWarnings,
          total);

      const std::size_t lastPage = (total + limit - 1) / limit;

      build::print_build_success(
          std::cout,
          "Listed warnings " +
              std::to_string(start + 1) +
              "-" +
              std::to_string(end) +
              " of " +
              std::to_string(total));

      if (page < lastPage)
      {
        hint("Next page: vix build --warnings --page " +
             std::to_string(page + 1) +
             " --limit " +
             std::to_string(limit));
      }
      return 0;
    }

    static void print_graph_warnings_modern(const std::string &output)
    {
      print_compiler_warnings_summary(output);
    }

    static std::string explain_display_path(const fs::path &path)
    {
      if (path.empty())
        return "<unknown>";

      return path.filename().string();
    }

    static bool graph_has_dirty_project_inputs_for_explain(
        const build::BuildGraph &graph)
    {
      for (const auto &kv : graph.nodes())
      {
        const build::BuildNode &node = kv.second;

        if (!(node.dirty() || node.missing()))
          continue;

        if (node.kind == build::BuildNodeKind::Source ||
            node.kind == build::BuildNodeKind::Header ||
            node.kind == build::BuildNodeKind::Config)
        {
          return true;
        }
      }

      return false;
    }

    static std::string explain_node_change(
        const build::BuildNode &current,
        const build::BuildNode *previous)
    {
      if (!previous)
        return "new input detected";

      if (current.state == build::BuildNodeState::Missing)
        return "input is missing";

      if (previous->state == build::BuildNodeState::Missing &&
          current.state != build::BuildNodeState::Missing)
      {
        return "input was restored";
      }

      if (current.hash != previous->hash)
        return "content hash changed";

      if (current.size != previous->size)
        return "file size changed";

      if (current.mtime != previous->mtime)
        return "file timestamp changed";

      if (current.state != previous->state)
        return "node state changed";

      return "";
    }

    static std::string explain_task_rebuild_reason(
        const build::BuildGraph &graph,
        const build::BuildGraph *previousGraph,
        const build::BuildTask &task)
    {
      if (!previousGraph)
        return "no previous build graph";

      const build::BuildTask *previousTask =
          previousGraph->find_task(task.id);

      if (!previousTask)
        return "new build task";

      if (task.commandHash != previousTask->commandHash)
        return "compiler command changed";

      for (const std::string &outputId : task.outputs)
      {
        const build::BuildNode *outputNode = graph.find_node(outputId);

        if (!outputNode)
          continue;

        if (!file_exists_regular(outputNode->path))
          return "output file is missing";
      }

      for (const std::string &inputId : task.inputs)
      {
        const build::BuildNode *currentNode = graph.find_node(inputId);

        if (!currentNode)
          continue;

        const build::BuildNode *previousNode =
            previousGraph->find_node(inputId);

        const std::string changeReason =
            explain_node_change(*currentNode, previousNode);

        if (changeReason.empty())
          continue;

        if (currentNode->kind == build::BuildNodeKind::Source)
          return "source file changed";

        if (currentNode->kind == build::BuildNodeKind::Header)
          return explain_display_path(currentNode->path) + " changed";

        if (currentNode->kind == build::BuildNodeKind::Config)
          return "build configuration changed";

        return changeReason;
      }

      return "dependency changed";
    }

    static void print_rebuild_explanation(
        const build::BuildGraph &graph,
        const build::BuildGraph *previousGraph,
        const process::Options &opt,
        const process::Plan &plan)
    {
      const std::vector<build::BuildTask> dirtyTasks =
          graph.dirty_compile_tasks();

      if (dirtyTasks.empty())
      {
        if (graph_has_dirty_project_inputs_for_explain(graph))
        {
          std::cout << "Project input changed\n";
          std::cout << "  reason: dependency changed, delegating target to Ninja\n\n";

          std::cout << "Relinking "
                    << build::default_build_target_name(opt, plan)
                    << "\n";

          std::cout << "  reason: target may depend on changed input\n\n";
          return;
        }

        std::cout << "No rebuild required\n";
        return;
      }

      bool printedAny = false;

      for (const build::BuildTask &task : dirtyTasks)
      {
        fs::path sourcePath;

        for (const std::string &inputId : task.inputs)
        {
          const build::BuildNode *node = graph.find_node(inputId);

          if (!node)
            continue;

          if (node->kind == build::BuildNodeKind::Source)
          {
            sourcePath = node->path;
            break;
          }
        }

        if (sourcePath.empty())
          continue;

        const std::string reason =
            explain_task_rebuild_reason(
                graph,
                previousGraph,
                task);

        std::cout << "Rebuilding "
                  << explain_display_path(sourcePath)
                  << "\n";

        std::cout << "  reason: "
                  << reason
                  << "\n\n";

        printedAny = true;
      }

      if (printedAny)
      {
        std::cout << "Relinking "
                  << build::default_build_target_name(opt, plan)
                  << "\n";

        std::cout << "  reason: object file changed\n\n";
      }
    }

    static build::BuildGraph make_build_graph_after_configure(
        const process::Options &opt,
        const process::Plan &plan,
        std::size_t &importedCompileCommands,
        std::size_t &importedNinjaTasks,
        build::BuildGraphScanResult &scan)
    {
      build::BuildGraphConfig graphConfig;
      graphConfig.projectDir = plan.userProjectDir;
      graphConfig.buildDir = plan.buildDir;
      graphConfig.objectDir = plan.buildDir / ".vix" / "obj";
      graphConfig.compiler = "c++";
      graphConfig.buildFingerprint =
          make_object_cache_build_fingerprint(plan, opt);

      graphConfig.includeDirs.push_back((plan.userProjectDir / "include").string());
      graphConfig.includeDirs.push_back((plan.userProjectDir / "src").string());

      graphConfig.flags.push_back("-Wall");
      graphConfig.flags.push_back("-Wextra");

      build::BuildGraph graph(graphConfig);

      scan = graph.scan_project();

      const fs::path compileCommandsPath =
          build::default_compile_commands_path(plan.buildDir);

      importedCompileCommands =
          graph.load_compile_commands(compileCommandsPath);

      const fs::path buildNinjaPath =
          build::default_build_ninja_path(plan.buildDir);

      importedNinjaTasks =
          graph.load_ninja_build(buildNinjaPath);

      graph.load_dependency_files();

      const fs::path graphPath =
          build::BuildGraph::default_graph_path(plan.buildDir);

      const auto previousGraph =
          build::BuildGraph::load(graphPath);

      graph.propagate_dirty();

      if (previousGraph)
        graph.mark_clean_from_previous(*previousGraph);
      else
        graph.mark_all_dirty();

      graph.propagate_dirty();

      (void)opt;

      return graph;
    }

    static std::vector<fs::path> graph_object_paths(const build::BuildGraph &graph)
    {
      std::vector<fs::path> objects;

      for (const build::BuildTask &task : graph.compile_tasks())
      {
        for (const auto &outputId : task.outputs)
        {
          const build::BuildNode *node = graph.find_node(outputId);
          if (!node)
            continue;

          if (node->kind != build::BuildNodeKind::Object)
            continue;

          if (!file_exists_regular(node->path))
            continue;

          objects.push_back(node->path);
        }
      }

      std::sort(objects.begin(), objects.end());
      objects.erase(std::unique(objects.begin(), objects.end()), objects.end());

      return objects;
    }

    static int run_graph_link(
        const build::BuildGraph &graph,
        const process::Options &opt,
        const process::Plan &plan,
        const fs::path &outputBinary)
    {
      (void)opt;
      const std::vector<fs::path> objects = graph_object_paths(graph);

      if (objects.empty())
      {
        error("Graph build produced no object files.");
        return 1;
      }

      std::vector<std::string> argv;
      argv.reserve(objects.size() + 8);

      argv.push_back(graph.config().compiler.empty() ? "c++" : graph.config().compiler);

      if (plan.fastLinkerFlag && !plan.fastLinkerFlag->empty())
        argv.push_back(*plan.fastLinkerFlag);

      for (const fs::path &object : objects)
        argv.push_back(object.string());

      argv.push_back("-o");
      argv.push_back(outputBinary.string());

      std::string output;
      const process::ExecResult r =
          build::run_process_capture(argv, {}, output);

      if (r.exitCode != 0)
      {
        error("Graph link failed.");
        if (!output.empty())
          std::cerr << output;
        return r.exitCode == 0 ? 1 : r.exitCode;
      }

#ifndef _WIN32
      std::error_code ec;
      fs::permissions(
          outputBinary,
          fs::perms::owner_exec |
              fs::perms::group_exec |
              fs::perms::others_exec,
          fs::perm_options::add,
          ec);
#endif

      return 0;
    }

    static int run_graph_build(
        build::BuildGraph &graph,
        const fs::path &graphPath,
        const process::Options &opt,
        const process::Plan &plan,
        const artifact_cache::Artifact &projectArtifact,
        const std::vector<artifact_cache::ProjectInput> &projectInputs,
        bool verboseMode,
        build::BuildLiveProcess *liveBuild)
    {
      {
        std::string err;
        if (!util::ensure_dir(graph.config().objectDir, err))
        {
          error("Unable to create Vix graph object directory: " +
                graph.config().objectDir.string());

          if (!err.empty())
            hint(err);

          return 1;
        }
      }

      build::ObjectCache objectCache(plan.buildDir);

      if (!objectCache.ensure_layout())
      {
        error("Unable to initialize Vix object cache.");
        return 1;
      }

      const fs::path outputBinary = graph_output_binary_path(opt, plan);
      const std::vector<build::BuildTask> dirtyTasks = graph.dirty_compile_tasks();

      const bool outputMissing = !file_exists_regular(outputBinary);
      const bool needsCompile = !dirtyTasks.empty();
      const bool needsLink = needsCompile || outputMissing;

      if (!needsCompile && !needsLink)
      {
        if (!graph.save(graphPath) && !opt.quiet)
          hint("Warning: unable to write Vix build graph");

        if (!opt.quiet)
        {
          build::print_build_success(std::cout, "Up to date");
          build::print_build_success(std::cout, "Done");
        }

        return 0;
      }

      if (verboseMode && !opt.quiet)
      {
        step("graph build: " + std::to_string(dirtyTasks.size()) + " dirty compile tasks");
      }

      if (needsCompile)
      {
        build::BuildSchedulerOptions schedulerOptions;
        schedulerOptions.jobs = opt.jobs;
        schedulerOptions.quiet = opt.quiet;
        schedulerOptions.stopOnFirstFailure = true;

        build::BuildScheduler scheduler(
            schedulerOptions);

        scheduler.add_tasks(
            dirtyTasks);

        std::atomic<std::size_t> startedCompileTasks{0};

        const std::size_t totalCompileTasks =
            dirtyTasks.size();

        const build::BuildSchedulerResult result =
            scheduler.run(
                [&](build::BuildTask &task)
                {
                  const std::size_t currentCompileTask =
                      startedCompileTasks.fetch_add(
                          1,
                          std::memory_order_relaxed) +
                      1;

                  if (liveBuild)
                  {
                    liveBuild->compile_progress(
                        currentCompileTask,
                        totalCompileTasks,
                        compile_task_source_subject(
                            graph,
                            task,
                            plan.userProjectDir),
                        build::default_build_target_name(
                            opt,
                            plan));
                  }

                  build::BuildTaskResult taskResult =
                      run_cached_graph_compile_task(
                          graph,
                          objectCache,
                          task);

                  if (!opt.quiet &&
                      !liveBuild &&
                      !taskResult.output.empty())
                  {
                    std::cout
                        << taskResult.output;
                  }

                  return taskResult;
                });

        if (!result.success())
        {
          for (const auto &taskResult : result.results)
          {
            if (!taskResult.output.empty())
              std::cerr << taskResult.output;
          }

          return 1;
        }
      }

      if (liveBuild)
      {
        liveBuild->link_started(
            build::default_build_target_name(
                opt,
                plan));
      }

      const int linkCode =
          run_graph_link(
              graph,
              opt,
              plan,
              outputBinary);

      if (linkCode == 0 &&
          liveBuild)
      {
        liveBuild->link_finished(
            build::default_build_target_name(
                opt,
                plan));
      }
      if (linkCode != 0)
      {
        return linkCode;
      }

      if (!store_project_target_artifact(projectArtifact, opt, plan) &&
          !opt.quiet &&
          debug_build_details_enabled(opt))
      {
        build::print_build_info(
            std::cout,
            "Artifact cache skipped: no main executable artifact found");
      }

      const auto state =
          artifact_cache::ArtifactCache::make_build_state(
              plan.signature,
              plan.projectFingerprint,
              projectArtifact.root.string(),
              outputBinary.string(),
              opt.buildTarget,
              plan.preset.name,
              plan.preset.buildType,
              projectArtifact.target,
              projectArtifact.compiler,
              projectInputs);

      if (!artifact_cache::ArtifactCache::write_build_state(plan.buildDir, state) &&
          !opt.quiet)
      {
        hint("Warning: unable to write Vix build state");
      }

      if (!graph.save(graphPath) && !opt.quiet)
        hint("Warning: unable to write Vix build graph");

      write_project_build_metadata(
          plan.userProjectDir,
          plan.buildDir,
          outputBinary,
          opt.sanitizerMode);

      if (opt.exportBin)
      {
        const fs::path dest = plan.userProjectDir / outputBinary.filename();

        if (!export_built_binary(outputBinary, dest, opt.quiet))
        {
          return 1;
        }
      }
      else if (!opt.outPath.empty())
      {
        write_last_binary(outputBinary);

        if (!opt.quiet)
          success("Exported binary: " + outputBinary.string());
      }
      else
      {
        write_last_binary(outputBinary);
      }

      if (!opt.quiet)
      {
        build::print_build_success(
            std::cout,
            needsCompile ? "Built with graph" : "Linked with graph");

        build::print_build_success(std::cout, "Done");
      }

      return 0;
    }

    static void print_vix_build_header(
        const std::string &action,
        const process::Options &opt,
        const process::Plan &plan)
    {
      build::print_task_header_full(
          std::cout,
          action,
          build::default_build_target_name(opt, plan),
          display_build_profile(plan),
          {});
    }

    static void print_vix_build_success(const std::string &message)
    {
      build::print_build_success(std::cout, message);
    }

    static void print_vix_build_success_timed(
        const std::string &message,
        long long milliseconds)
    {
      build::print_task_success_timed(
          std::cout,
          message,
          milliseconds);
    }

    static bool can_use_native_vix_app_build(
        const process::Options &opt,
        const app::AppManifest &manifest)
    {
      if (!opt.useCache)
        return false;

      if (opt.clean)
        return false;

      if (vix::engine::sanitizer_enabled(opt.sanitizerMode))
        return false;

      if (!opt.targetTriple.empty())
        return false;

      if (!opt.cmakeArgs.empty())
        return false;

      if (opt.withSqlite || opt.withMySql)
        return false;

      if (opt.linkStatic)
        return false;

      if (!opt.buildTarget.empty() && opt.buildTarget != manifest.name)
        return false;

      if (manifest.type != app::AppTargetType::Executable)
        return false;

      if (!manifest.links.empty())
        return false;

      if (!manifest.deps.empty() || !manifest.gitDependencies.empty())
        return false;

      if (!manifest.packages.empty())
        return false;

      if (!manifest.resources.empty())
        return false;

      if (!manifest.compileFeatures.empty())
        return false;

      if (manifest.sources.empty())
        return false;

      return true;
    }

    static fs::path native_vix_app_build_dir(
        const fs::path &projectDir,
        const process::Options &opt)
    {
      if (opt.preset == "release")
        return projectDir / ".vix" / "native" / "release";

      return projectDir / ".vix" / "native" / "dev";
    }

    static fs::path native_vix_app_output_path(
        const fs::path &projectDir,
        const fs::path &buildDir,
        const app::AppManifest &manifest)
    {
      if (!manifest.outputDir.empty())
        return projectDir / manifest.outputDir / platform_executable_name(manifest.name);

      return buildDir / platform_executable_name(manifest.name);
    }

    static std::string native_cpp_standard_flag(const std::string &standard)
    {
      if (standard == "c++11" || standard == "cpp11" || standard == "11")
        return "-std=c++11";

      if (standard == "c++14" || standard == "cpp14" || standard == "14")
        return "-std=c++14";

      if (standard == "c++17" || standard == "cpp17" || standard == "17")
        return "-std=c++17";

      if (standard == "c++20" || standard == "cpp20" || standard == "20")
        return "-std=c++20";

      if (standard == "c++23" || standard == "cpp23" || standard == "23")
        return "-std=c++23";

      if (standard == "c++26" || standard == "cpp26" || standard == "26")
        return "-std=c++26";

      return "-std=c++20";
    }

    static std::vector<std::string> native_vix_app_compile_command(
        const fs::path &projectDir,
        const fs::path &source,
        const fs::path &object,
        const app::AppManifest &manifest,
        const process::Plan &plan)
    {
      std::vector<std::string> command;

      command.push_back("c++");
      command.push_back(native_cpp_standard_flag(manifest.standard));

      if (plan.preset.buildType == "Release")
      {
        command.push_back("-O2");
        command.push_back("-DNDEBUG");
      }
      else
      {
        command.push_back("-g");
        command.push_back("-O0");
      }

      command.push_back("-MMD");
      command.push_back("-MP");

      const fs::path dependencyFile =
          build::dependency_file_for_object(object);

      command.push_back("-MF");
      command.push_back(dependencyFile.string());

      for (const std::string &dir : manifest.includeDirs)
      {
        command.push_back("-I");
        command.push_back((projectDir / dir).lexically_normal().string());
      }

      for (const std::string &define : manifest.defines)
        command.push_back("-D" + define);

      for (const std::string &option : manifest.compileOptions)
        command.push_back(option);

      command.push_back("-c");
      command.push_back((projectDir / source).lexically_normal().string());

      command.push_back("-o");
      command.push_back(object.string());

      return command;
    }

    static std::vector<std::string> native_vix_app_link_command(
        const std::vector<fs::path> &objects,
        const fs::path &outputBinary,
        const app::AppManifest &manifest,
        const process::Plan &plan)
    {
      std::vector<std::string> command;

      command.push_back("c++");

      if (plan.fastLinkerFlag)
        command.push_back(*plan.fastLinkerFlag);

      for (const fs::path &object : objects)
        command.push_back(object.string());

      for (const std::string &option : manifest.linkOptions)
        command.push_back(option);

      command.push_back("-o");
      command.push_back(outputBinary.string());

      return command;
    }

    struct NativeVixAppBuildSession
    {
      process::Plan plan;
      build::BuildGraph graph;
      std::vector<fs::path> objectPaths;
      fs::path outputBinary;
      std::map<std::string, std::string> sourceTaskIds;
    };

    static bool native_vix_app_is_source_path(const fs::path &path)
    {
      const std::string ext = path.extension().string();
      return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c";
    }

    static bool prepare_native_vix_app_build_session(
        const process::Options &opt,
        const fs::path &projectDir,
        const app::AppManifest &manifest,
        NativeVixAppBuildSession &session,
        int &exitCode)
    {
      exitCode = 0;

      const auto presetOpt = build::resolve_builtin_preset(opt.preset);

      if (!presetOpt)
      {
        error("Unknown preset: " + opt.preset);
        exitCode = 2;
        return false;
      }

      process::Plan plan;
      plan.userProjectDir = projectDir;
      plan.projectDir = projectDir;
      plan.cmakeSourceDir = projectDir;
      plan.defaultTargetName = manifest.name;
      plan.generatedFromVixApp = false;
      plan.preset = *presetOpt;
      plan.buildDir = native_vix_app_build_dir(projectDir, opt);
      plan.launcher = detect_launcher(opt);
      plan.fastLinkerFlag = detect_fast_linker_flag(opt);

      std::string err;

      if (!util::ensure_dir(plan.buildDir, err))
      {
        error("Unable to create native vix.app build directory: " + plan.buildDir.string());

        if (!err.empty())
          hint(err);

        exitCode = 1;
        return false;
      }

      const fs::path objectDir = plan.buildDir / "obj";

      if (!util::ensure_dir(objectDir, err))
      {
        error("Unable to create native vix.app object directory: " + objectDir.string());

        if (!err.empty())
          hint(err);

        exitCode = 1;
        return false;
      }

      const fs::path outputBinary =
          native_vix_app_output_path(projectDir, plan.buildDir, manifest);

      if (!outputBinary.parent_path().empty() &&
          !util::ensure_dir(outputBinary.parent_path(), err))
      {
        error("Unable to create native vix.app output directory: " +
              outputBinary.parent_path().string());

        if (!err.empty())
          hint(err);

        exitCode = 1;
        return false;
      }

      build::BuildGraphConfig graphConfig;
      graphConfig.projectDir = projectDir;
      graphConfig.buildDir = plan.buildDir;
      graphConfig.objectDir = objectDir;
      graphConfig.compiler = "c++";
      graphConfig.buildFingerprint =
          make_object_cache_build_fingerprint(plan, opt);

      build::BuildGraph graph(graphConfig);

      std::vector<fs::path> objectPaths;
      objectPaths.reserve(manifest.sources.size());
      std::map<std::string, std::string> sourceTaskIds;

      for (const std::string &sourceString : manifest.sources)
      {
        const fs::path sourceRel(sourceString);
        const fs::path sourcePath =
            (projectDir / sourceRel).lexically_normal();

        if (!fs::exists(sourcePath))
        {
          error("Source file not found: " + sourcePath.string());
          exitCode = 1;
          return false;
        }

        std::string objectName =
            sourceRel.lexically_normal().generic_string();

        for (char &c : objectName)
        {
          const unsigned char uc = static_cast<unsigned char>(c);

          if (!(std::isalnum(uc) || c == '.' || c == '_' || c == '-'))
            c = '_';
        }

        const fs::path objectPath =
            objectDir / (objectName + ".o");

        objectPaths.push_back(objectPath);

        build::BuildNode sourceNode =
            build::make_file_build_node(
                build::BuildNodeKind::Source,
                sourcePath);

        build::BuildNode objectNode =
            build::make_file_build_node(
                build::BuildNodeKind::Object,
                objectPath);

        const std::vector<std::string> command =
            native_vix_app_compile_command(
                projectDir,
                sourceRel,
                objectPath,
                manifest,
                plan);

        build::BuildTask task =
            build::make_compile_task(
                sourceNode.id,
                objectNode.id,
                command,
                projectDir);

        graph.add_node(sourceNode);
        graph.add_node(objectNode);
        graph.add_task(task);
        sourceTaskIds[sourcePath.lexically_normal().generic_string()] = task.id;
      }

      session.plan = std::move(plan);
      session.graph = std::move(graph);
      session.objectPaths = std::move(objectPaths);
      session.outputBinary = outputBinary;
      session.sourceTaskIds = std::move(sourceTaskIds);
      return true;
    }

    static int run_native_vix_app_tasks(
        const process::Options &opt,
        NativeVixAppBuildSession &session,
        const std::vector<std::string> &taskIds,
        WatchProgressLine *progress = nullptr,
        const std::string &progressDetail = {},
        build::BuildLiveProcess *liveBuild = nullptr)
    {
      build::ObjectCache objectCache(session.plan.buildDir);

      if (!objectCache.ensure_layout())
      {
        error("Unable to initialize Vix object cache.");
        return 1;
      }

      build::BuildSchedulerOptions schedulerOptions;
      schedulerOptions.jobs = opt.jobs;
      schedulerOptions.quiet = opt.quiet;
      schedulerOptions.stopOnFirstFailure = true;

      build::BuildScheduler scheduler(
          schedulerOptions);

      std::size_t totalCompileTasks = 0;

      if (taskIds.empty())
      {
        const auto compileTasks =
            session.graph.compile_tasks();

        totalCompileTasks =
            compileTasks.size();

        scheduler.add_tasks(
            compileTasks);
      }
      else
      {
        for (const std::string &taskId : taskIds)
        {
          build::BuildTask *task =
              session.graph.find_task(
                  taskId);

          if (!task)
            continue;

          scheduler.add_task(
              *task);

          ++totalCompileTasks;
        }
      }

      std::atomic<std::size_t>
          startedCompileTasks{0};

      const build::BuildSchedulerResult result =
          scheduler.run(
              [&](build::BuildTask &task)
              {
                const std::string sourceSubject =
                    compile_task_source_subject(
                        session.graph,
                        task,
                        session.plan.userProjectDir);

                if (progress)
                {
                  progress->update(
                      "Building",
                      sourceSubject,
                      progressDetail);
                }

                if (liveBuild)
                {
                  const std::size_t currentCompileTask =
                      startedCompileTasks.fetch_add(
                          1,
                          std::memory_order_relaxed) +
                      1;

                  liveBuild->compile_progress(
                      currentCompileTask,
                      totalCompileTasks,
                      sourceSubject,
                      build::default_build_target_name(
                          opt,
                          session.plan));
                }

                build::BuildTaskResult taskResult =
                    run_cached_graph_compile_task(
                        session.graph,
                        objectCache,
                        task);

                if (!opt.quiet &&
                    !liveBuild &&
                    !taskResult.output.empty())
                {
                  std::cout
                      << taskResult.output;
                }

                return taskResult;
              });

      if (!result.success())
      {
        if (progress)
          progress->stop();

        if (liveBuild)
        {
          liveBuild->finish(1);
        }

        for (const auto &taskResult : result.results)
        {
          if (!taskResult.output.empty())
          {
            std::cerr
                << taskResult.output;
          }
        }

        return 1;
      }

      return 0;
    }

    static int link_native_vix_app_build(
        const process::Options &opt,
        const fs::path &projectDir,
        const app::AppManifest &manifest,
        NativeVixAppBuildSession &session,
        WatchProgressLine *progress = nullptr,
        const std::string &progressDetail = {},
        build::BuildLiveProcess *liveBuild = nullptr)
    {
      const std::string linkTarget =
          manifest.name.empty()
              ? std::string("vix.app")
              : manifest.name;

      if (progress)
      {
        progress->update(
            "Linking",
            linkTarget,
            progressDetail);
      }

      if (liveBuild)
      {
        liveBuild->link_started(
            linkTarget);
      }

      const std::vector<std::string> linkCommand =
          native_vix_app_link_command(
              session.objectPaths,
              session.outputBinary,
              manifest,
              session.plan);

      std::string linkOutput;

      const process::ExecResult linkResult =
          build::run_process_capture(
              linkCommand,
              {},
              linkOutput);

      if (linkResult.exitCode != 0)
      {
        const int exitCode =
            linkResult.exitCode == 0
                ? 1
                : linkResult.exitCode;

        if (liveBuild)
        {
          liveBuild->finish(
              exitCode);
        }

        error(
            "Native vix.app link failed.");

        if (!linkOutput.empty())
        {
          std::cerr
              << linkOutput;
        }

        return exitCode;
      }

      if (liveBuild)
      {
        liveBuild->link_finished(
            linkTarget);
      }

#ifndef _WIN32
      std::error_code ec;
      fs::permissions(
          session.outputBinary,
          fs::perms::owner_exec |
              fs::perms::group_exec |
              fs::perms::others_exec,
          fs::perm_options::add,
          ec);
#endif

      write_project_build_metadata(
          projectDir,
          session.plan.buildDir,
          session.outputBinary,
          opt.sanitizerMode);

      if (opt.exportBin || !opt.outPath.empty())
      {
        fs::path dest;

        if (opt.exportBin)
          dest = projectDir / session.outputBinary.filename();
        else
          dest = fs::absolute(fs::path(opt.outPath));

        if (!export_built_binary(session.outputBinary, dest, opt.quiet))
          return 1;
      }

      return 0;
    }

    static std::vector<std::string> native_vix_app_task_ids_for_batch(
        const NativeVixAppBuildSession &session,
        const vix::engine::watch::Batch &batch)
    {
      std::vector<std::string> taskIds;
      std::set<std::string> seen;

      for (const auto &event : batch.events)
      {
        if (!native_vix_app_is_source_path(event.path))
          continue;

        const std::string key =
            event.path.lexically_normal().generic_string();
        const auto it = session.sourceTaskIds.find(key);

        if (it == session.sourceTaskIds.end())
          continue;

        if (seen.insert(it->second).second)
          taskIds.push_back(it->second);
      }

      return taskIds;
    }

    static int run_native_vix_app_build(
        const process::Options &opt,
        const fs::path &projectDir,
        const app::AppManifest &manifest,
        const std::chrono::steady_clock::time_point &commandStart,
        bool livePresentation = true)
    {
      NativeVixAppBuildSession session;
      int prepareExit = 0;

      if (!prepare_native_vix_app_build_session(
              opt,
              projectDir,
              manifest,
              session,
              prepareExit))
      {
        return prepareExit;
      }

      std::optional<build::BuildLiveProcess>
          liveBuild;

      if (livePresentation &&
          !opt.quiet)
      {
        liveBuild.emplace(
            std::cout);

        liveBuild->begin(
            build::default_build_target_name(
                opt,
                session.plan));
      }
      else if (!opt.quiet)
      {
        /*
         * Keep the existing presentation for watch mode, which owns its
         * independent WatchProgressLine lifecycle.
         */
        print_vix_build_header(
            "Building",
            opt,
            session.plan);
      }

      const int compileCode =
          run_native_vix_app_tasks(
              opt,
              session,
              {},
              nullptr,
              {},
              liveBuild
                  ? &*liveBuild
                  : nullptr);

      if (compileCode != 0)
      {
        if (liveBuild)
        {
          liveBuild->finish(
              compileCode);
        }

        return compileCode;
      }

      const int linkCode =
          link_native_vix_app_build(
              opt,
              projectDir,
              manifest,
              session,
              nullptr,
              {},
              liveBuild
                  ? &*liveBuild
                  : nullptr);

      if (linkCode != 0)
      {
        if (liveBuild)
        {
          liveBuild->finish(
              linkCode);
        }

        return linkCode;
      }

      if (liveBuild)
      {
        liveBuild->finish(0);
      }
      else if (!opt.quiet)
      {
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - commandStart)
                .count();

        print_vix_build_success(
            "Native vix.app");

        print_vix_build_success_timed(
            "Done",
            ms);
      }

      return 0;
    }

    class BuildCommand
    {
    public:
      explicit BuildCommand(process::Options opt) : opt_(std::move(opt)) {}

      int run()
      {
        if (opt_.watch)
          return run_watch();

        const fs::path cwd = fs::current_path();
        const auto commandStart = std::chrono::steady_clock::now();
        std::vector<BuildPhaseTiming> phaseTimings;
        auto measurePhase =
            [&](const std::string &name, const auto &fn)
        {
          const auto t0 = std::chrono::steady_clock::now();
          auto result = fn();
          const auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
          if (opt_.explain)
            phaseTimings.push_back({name, ms});
          return result;
        };

        if (opt_.singleCpp)
          return run_single_cpp_build();

        {
          fs::path base = cwd;

          if (!opt_.dir.empty())
            base = fs::absolute(fs::path(opt_.dir));

          const app::AppProjectResolveResult project =
              app::resolve_app_project(base);

          if (project.success() &&
              project.kind == app::AppProjectKind::VixApp)
          {
            const app::AppManifestLoadResult loadResult =
                app::load_app_manifest(project.appManifestPath);

            if (!loadResult.success())
            {
              error("Failed to load vix.app.");
              hint(loadResult.error);
              return 1;
            }

            if (!opt_.warnings && !opt_.showLog &&
                can_use_native_vix_app_build(opt_, loadResult.manifest))
            {
              return run_native_vix_app_build(
                  opt_,
                  project.userProjectDir,
                  loadResult.manifest,
                  commandStart);
            }

            if (debug_build_details_enabled(opt_) && !opt_.quiet)
              hint("Native vix.app fallback: generated CMake path");
          }
        }

        const auto resolvedPlanOpt =
            measurePhase(
                "resolve project",
                [&]()
                {
                  return make_plan(opt_, cwd);
                });
        if (!resolvedPlanOpt)
        {
          error("Unable to determine the project directory (missing CMakeLists.txt?)");
          hint("Run from your project root, or pass: vix build --dir <path>");
          return 1;
        }

        plan_ = resolvedPlanOpt->plan;

        if (!resolvedPlanOpt->sdkResolutionError.empty())
        {
          error(resolvedPlanOpt->sdkResolutionError);
          return 1;
        }

        if (opt_.warnings)
        {
          return print_last_build_warnings(
              opt_,
              plan_);
        }

        if (opt_.showLog)
        {
          const auto print_log = [](const fs::path &path, const std::string &label) -> bool
          {
            const std::string text = util::read_text_file_or_empty(path);
            if (text.empty())
              return false;
            std::cout << label << " log\n\n"
                      << text;
            if (text.back() != '\n')
              std::cout << '\n';
            return true;
          };

          bool found = false;
          if (!opt_.logPath.empty())
          {
            const fs::path requested = opt_.logPath;
            std::error_code ec;
            if (!fs::exists(requested, ec) || ec)
            {
              error("Build log path not found: " + requested.string());
              return 1;
            }
            if (fs::is_regular_file(requested, ec))
              found = print_log(requested, "Build");
            else if (fs::is_directory(requested, ec))
            {
              const fs::path canonicalBuildLog = requested / "build.log";
              if (fs::is_regular_file(canonicalBuildLog, ec) && !ec)
              {
                found = print_log(canonicalBuildLog, "Build");
              }
              else
              {
                fs::path newest;
                fs::file_time_type newestTime{};
                for (const auto &entry : fs::directory_iterator(requested, ec))
                {
                  if (ec || !entry.is_regular_file(ec) || entry.path().extension() != ".log")
                    continue;
                  const auto time = entry.last_write_time(ec);
                  if (!ec && (newest.empty() || time > newestTime))
                  {
                    newest = entry.path();
                    newestTime = time;
                  }
                }
                if (!newest.empty())
                  found = print_log(newest, "Build");
              }
              if (!found)
              {
                error("No build logs found in " + requested.string());
                return 1;
              }
            }
          }
          else if (opt_.logScope == "configure" || opt_.logScope == "all")
            found = print_log(plan_.configureLog, "Configure") || found;
          if (opt_.logScope.empty() || opt_.logScope == "build" || opt_.logScope == "all")
            found = print_log(plan_.buildLog, "Build") || found;
          if (!found)
          {
            error("No previous build log found.");
            hint("Run `vix build` first.");
            return 1;
          }
          return 0;
        }

        const fs::path globalPackagesFile =
            plan_.buildDir / "vix-global-packages.cmake";

        const bool debugMode =
            debug_build_details_enabled(opt_);

        const bool verboseMode =
            opt_.verbose ||
            debugMode;

        const bool rawBuildOutput =
            opt_.cmakeVerbose;
        const bool defer = false;
        DeferredConsole out(defer);
        bool buildHeaderPrinted = false;
        auto printBuildHeaderEarly =
            [&]()
        {
          if (opt_.quiet || buildHeaderPrinted)
            return;

          if (verboseMode)
          {
            const std::optional<std::string> effectiveLauncher =
                plan_.launcher ? plan_.launcher : std::optional<std::string>{"none"};
            build::print_build_header_full(
                std::cout,
                build::default_build_target_name(opt_, plan_),
                display_build_profile(plan_),
                effectiveLauncher,
                plan_.fastLinkerFlag,
                opt_.jobs <= 0 ? build::default_jobs() : opt_.jobs);
          }
          else
          {
            build::print_build_header_full(
                std::cout,
                build::default_build_target_name(opt_, plan_),
                display_build_profile(plan_),
                std::nullopt,
                std::nullopt,
                0);
          }

          buildHeaderPrinted = true;
          std::cout.flush();
        };

        std::string tc;

#ifndef _WIN32
        if (!util::executable_on_path("ld"))
        {
          if (!opt_.quiet)
          {
            hint("System linker 'ld' not found. Build may fail at link step.");
            hint("Fix (recommended): sudo apt install -y binutils build-essential");
          }
        }

        if (plan_.fastLinkerFlag &&
            *plan_.fastLinkerFlag == "-fuse-ld=lld" &&
            !util::executable_on_path("ld.lld"))
        {
          if (!opt_.quiet)
          {
            hint("Requested lld but 'ld.lld' is missing -> falling back to default linker.");
            hint("Install optional speedup: sudo apt install -y lld");
          }

          plan_.fastLinkerFlag.reset();
        }

        if (plan_.fastLinkerFlag &&
            *plan_.fastLinkerFlag == "-fuse-ld=mold" &&
            !util::executable_on_path("mold"))
        {
          if (!opt_.quiet)
          {
            hint("Requested mold but 'mold' is missing -> falling back to default linker.");
            hint("Install optional speedup: sudo apt install -y mold");
          }

          plan_.fastLinkerFlag.reset();
        }

        plan_.cmakeVars = build_cmake_vars(
            plan_.preset,
            opt_,
            plan_.toolchainFile,
            plan_.launcher,
            plan_.fastLinkerFlag,
            globalPackagesFile,
            plan_.dependencyEnvironmentMode,
            plan_.sdkConfigDir);

        if (!opt_.targetTriple.empty())
        {
          tc = build::toolchain_contents_for_triple(
              opt_.targetTriple,
              opt_.sysroot);
        }

        plan_.signature = build_configuration_signature(plan_, opt_, tc);
#endif

        if (!opt_.targetTriple.empty())
        {
          const std::string gcc = opt_.targetTriple + "-gcc";
          const std::string gxx = opt_.targetTriple + "-g++";

          if (!util::executable_on_path(gcc) ||
              !util::executable_on_path(gxx))
          {
            error("Cross toolchain not found on PATH for target: " + opt_.targetTriple);
            hint("Install the cross compiler and ensure binaries exist:");
            hint("  " + gcc);
            hint("  " + gxx);
            return 1;
          }
        }

        {
          std::string err;
          if (!util::ensure_dir(plan_.buildDir, err))
          {
            error("Unable to create build directory: " + plan_.buildDir.string());

            if (!err.empty())
              hint(err);

            return 1;
          }
        }

        printBuildHeaderEarly();

        if (!opt_.targetTriple.empty() && !opt_.quiet)
          step("target: " + opt_.targetTriple + " (cross)");

        artifact_cache::Artifact projectArtifact =
            make_project_artifact(plan_, opt_, tc);

        const auto previousState =
            artifact_cache::ArtifactCache::read_build_state(plan_.buildDir);

        const bool canFastNoopCheck =
            measurePhase(
                "up-to-date check",
                [&]()
                {
                  return opt_.useCache &&
                         !opt_.clean &&
                         previousState &&
                         previousState->signature == plan_.signature &&
                         previousState->projectFingerprint == plan_.projectFingerprint &&
                         previousState->buildTarget == opt_.buildTarget &&
                         previousState->preset == plan_.preset.name &&
                         previousState->buildType == plan_.preset.buildType &&
                         previousState->target == projectArtifact.target &&
                         previousState->compiler == projectArtifact.compiler &&
                         !previousState->lastBinary.empty() &&
                         util::file_exists(previousState->lastBinary) &&
                         previous_project_inputs_still_current(
                             plan_.userProjectDir,
                             previousState->inputs) &&
                         cmake_globs_still_current(plan_.buildDir);
                });

        if (previousState && !canFastNoopCheck && debug_build_details_enabled(opt_) && !opt_.quiet)
        {
          if (previousState->signature != plan_.signature)
            step("fast no-op miss: signature changed");
          else if (previousState->projectFingerprint != plan_.projectFingerprint)
            step("fast no-op miss: project fingerprint changed");
          else if (previousState->buildTarget != opt_.buildTarget)
            step("fast no-op miss: build target changed");
          else if (previousState->preset != plan_.preset.name)
            step("fast no-op miss: preset changed");
          else if (previousState->buildType != plan_.preset.buildType)
            step("fast no-op miss: build type changed");
          else if (previousState->target != projectArtifact.target)
            step("fast no-op miss: target changed");
          else if (previousState->compiler != projectArtifact.compiler)
            step("fast no-op miss: compiler changed");
          else if (previousState->lastBinary.empty() || !util::file_exists(previousState->lastBinary))
            step("fast no-op miss: last binary missing");
          else if (const std::string changedInput = first_changed_project_input(plan_.userProjectDir, previousState->inputs); !changedInput.empty())
            step("fast no-op miss: project input changed: " + changedInput);
          else if (!cmake_globs_still_current(plan_.buildDir))
            step("fast no-op miss: CMake glob changed");
        }

        if (canFastNoopCheck &&
            opt_.fast &&
            !opt_.explain &&
            !opt_.exportBin &&
            opt_.outPath.empty())
        {
          const auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - commandStart)
                  .count();

          if (!opt_.quiet)
          {
            build_print_phase_timings(phaseTimings);
            if (!buildHeaderPrinted)
              print_vix_build_header("Checking", opt_, plan_);
            print_vix_build_success_timed("Up to date", ms);
          }

          return 0;
        }

        std::vector<artifact_cache::ProjectInput> projectInputs =
            artifact_cache::ArtifactCache::snapshot_project_inputs(
                plan_.userProjectDir,
                previousState ? &previousState->inputs : nullptr);

        const bool buildStateHit =
            opt_.useCache &&
            !opt_.clean &&
            previousState &&
            artifact_cache::ArtifactCache::build_state_matches(
                *previousState,
                plan_.signature,
                plan_.projectFingerprint,
                opt_.buildTarget,
                plan_.preset.name,
                plan_.preset.buildType,
                projectArtifact.target,
                projectArtifact.compiler,
                projectInputs);

        if (buildStateHit && debug_build_details_enabled(opt_) && !opt_.quiet)
        {
          step("build state: hit -> " +
               artifact_cache::ArtifactCache::build_state_path(plan_.buildDir).string());
        }

        if (buildStateHit &&
            opt_.fast &&
            !opt_.explain &&
            !opt_.exportBin &&
            opt_.outPath.empty())
        {
          const auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - commandStart)
                  .count();

          if (!opt_.quiet)
          {
            build_print_phase_timings(phaseTimings);
            if (!buildHeaderPrinted)
              print_vix_build_header("Checking", opt_, plan_);
            print_vix_build_success_timed("Up to date", ms);
          }

          return 0;
        }

        if (debug_build_details_enabled(opt_) && !opt_.quiet)
        {
          if (artifact_cache::ArtifactCache::exists(projectArtifact))
            step("artifact cache: hit -> " + projectArtifact.root.string());
          else
            step("artifact cache: miss -> " + projectArtifact.root.string());

          out.print("  Using project directory:\n");
          out.print("    • " + plan_.userProjectDir.string() + "\n");

          if (plan_.generatedFromVixApp)
          {
            out.print("  Using generated CMake source:\n");
            out.print("    • " + plan_.cmakeSourceDir.string() + "\n");
          }

          out.print("\n");
        }

        if (!opt_.targetTriple.empty())
        {
          tc = build::toolchain_contents_for_triple(
              opt_.targetTriple,
              opt_.sysroot);

          projectArtifact = make_project_artifact(plan_, opt_, tc);

          if (!write_if_different(plan_.toolchainFile, tc))
          {
            error("Failed to write toolchain file: " + plan_.toolchainFile.string());
            hint("Check filesystem permissions.");
            return 1;
          }
        }

        /*
         * CMake/Ninja already owns the established Vix build presentation in
         * run_process_live_to_log(): it converts Ninja's [n/total] status
         * into the live build bar and freezes it as "build ... done".  The
         * event facade remains available to the graph paths below, but it
         * must not replace that presentation for the regular CMake build.
         */
        std::optional<build::BuildLiveProcess> liveBuild;

        bool configuredThisRun = false;

        const vix::engine::ConfigureDecision configureDecision =
            measurePhase(
                "configuration check",
                [&]()
                {
                  return evaluate_configure_decision(opt_, plan_);
                });

        if (configureDecision.needs_configure())
        {
          if (liveBuild)
            liveBuild->begin_configure();

          if (verboseMode && !opt_.quiet)
          {
            out.print("Configuring " + build::default_build_target_name(opt_, plan_) +
                      " (" + display_build_profile(plan_) + ")\n");

            if (debug_build_details_enabled(opt_))
            {
              if (plan_.launcher)
                out.print("  • compiler cache: " + *plan_.launcher + "\n");

              if (plan_.fastLinkerFlag)
                out.print("  • fast linker: " + *plan_.fastLinkerFlag + "\n");

              for (const auto &kv : plan_.cmakeVars)
                out.print("  • " + kv.first + "=" + kv.second + "\n");

              out.print("\n");
            }
          }

          const auto t0 = std::chrono::steady_clock::now();
          const auto argv = build::cmake_configure_argv(plan_, opt_);

          const process::ExecResult r =
              build::run_process_live_to_log(
                  argv,
                  {},
                  plan_.configureLog,
                  (opt_.quiet || !opt_.cmakeVerbose),
                  opt_.cmakeVerbose,
                  false,
                  liveBuild
                      ? liveBuild->observer()
                      : build::BuildOutputObserver{},
                  opt_.heartbeat);

          if (r.exitCode != 0)
          {
            out.discard();

            const int exitCode =
                (r.exitCode == 0)
                    ? 2
                    : r.exitCode;

            if (liveBuild)
              liveBuild->finish(exitCode);

            const std::string log =
                util::read_text_file_or_empty(
                    plan_.configureLog);

            const bool handled =
                vix::cli::ErrorHandler::printBuildErrors(
                    log,
                    plan_.cmakeSourceDir / "CMakeLists.txt",
                    "CMake configure failed");

            if (!handled && opt_.verbose && !log.empty())
            {
              std::cerr << "\nCMake output:\n";
              std::cerr << log << "\n";
            }

            if (!opt_.quiet)
            {
              if (!handled)
                hint("run `vix build --log configure` for the captured configure output");

              print_debug_command_if_enabled(opt_, r);
            }

            return exitCode;
          }

          if (opt_.useCache)
          {
            if (!vix::engine::write_configuration_signature(
                    plan_.sigFile,
                    plan_.signature))
            {
              if (!opt_.quiet)
                hint("Warning: unable to write config signature file");
            }
          }

          const auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();

          if (!opt_.quiet && verboseMode)
          {
            out.print(PAD + std::string(GREEN) + "✔ Configured in " + RESET +
                      util::format_seconds(ms) + "\n");
          }
        }
        else
        {
          if (debug_build_details_enabled(opt_) && !opt_.quiet)
          {
            out.print("  Using existing configuration (cache-friendly).\n");
            out.print("    • " + plan_.buildDir.string() + "\n\n");
          }
        }

        std::size_t importedCompileCommands = 0;
        std::size_t importedNinjaTasks = 0;
        build::BuildGraphScanResult scan{};

        build::BuildGraph graph =
            measurePhase(
                "graph load",
                [&]()
                {
                  return make_build_graph_after_configure(
                      opt_,
                      plan_,
                      importedCompileCommands,
                      importedNinjaTasks,
                      scan);
                });

        const fs::path graphPath =
            build::BuildGraph::default_graph_path(plan_.buildDir);

        if (debug_build_details_enabled(opt_) && !opt_.quiet)
        {
          step("build graph: " +
               std::to_string(scan.sources) + " sources, " +
               std::to_string(scan.headers) + " headers, " +
               std::to_string(graph.compile_tasks().size()) + " compile tasks, " +
               std::to_string(importedCompileCommands) + " imported commands, " +
               std::to_string(importedNinjaTasks) + " ninja tasks");
        }

        if (!graph_executor_enabled(opt_) &&
            can_use_target_artifact_cache(opt_) &&
            restore_project_target_artifact(projectArtifact, opt_, plan_))
        {
          if (!graph.save(graphPath) && !opt_.quiet)
            hint("Warning: unable to write Vix build graph");

          const fs::path restoredBinary =
              build::default_project_executable_path(opt_, plan_);

          const auto state =
              artifact_cache::ArtifactCache::make_build_state(
                  plan_.signature,
                  plan_.projectFingerprint,
                  projectArtifact.root.string(),
                  restoredBinary.string(),
                  opt_.buildTarget,
                  plan_.preset.name,
                  plan_.preset.buildType,
                  projectArtifact.target,
                  projectArtifact.compiler,
                  projectInputs);

          if (!artifact_cache::ArtifactCache::write_build_state(plan_.buildDir, state) &&
              !opt_.quiet)
          {
            hint("Warning: unable to write Vix build state");
          }

          if (liveBuild)
          {
            print_vix_build_success(
                "Artifact cache hit");

            liveBuild->finish(0);
          }

          return 0;
        }

        std::optional<build::BuildGraph> previousGraphForExplain;

        if (opt_.explain)
        {
          previousGraphForExplain =
              build::BuildGraph::load(graphPath);

          print_rebuild_explanation(
              graph,
              previousGraphForExplain ? &*previousGraphForExplain : nullptr,
              opt_,
              plan_);
        }

        if (can_use_target_graph_executor(
                opt_,
                importedCompileCommands,
                importedNinjaTasks))
        {
          build::BuildGraphExecutorOptions executorOptions;
          executorOptions.buildDir = plan_.buildDir;
          executorOptions.target = build::default_graph_target_name(opt_, plan_);
          executorOptions.jobs = opt_.jobs;

          build::BuildGraphExecutorDependencies executorDependencies;
          executorDependencies.executeCompileTask =
              [](build::BuildTask &task)
          {
            return build::execute_build_task_process(task);
          };
          executorDependencies.executeNinjaTarget =
              [&](const build::BuildGraphExecutorNinjaRequest &request)
          {
            return build::execute_graph_ninja_target(
                request,
                !rawBuildOutput,
                liveBuild
                    ? liveBuild->observer()
                    : build::BuildOutputObserver{});
          };

          executorDependencies.onEvent =
              [&](const build::BuildGraphExecutorEvent &event)
          {
            if (liveBuild &&
                event.kind ==
                    build::BuildGraphExecutorEventKind::CompilingTask &&
                !event.taskId.empty())
            {
              liveBuild->compile_progress(
                  event.current,
                  event.total,
                  compile_task_subject_for_id(
                      graph,
                      event.taskId,
                      plan_.userProjectDir),
                  event.target);
            }

            build::render_graph_debug_event(
                event,
                opt_.quiet,
                verboseMode);
          };

          build::BuildGraphExecutor executor(
              executorOptions,
              std::move(executorDependencies));

          const build::BuildGraphExecutorResult graphResult =
              measurePhase(
                  "build",
                  [&]()
                  {
                    return executor.run_target(graph);
                  });

          if (graphResult.ok)
          {
            if (!store_project_target_artifact(projectArtifact, opt_, plan_) &&
                !opt_.quiet &&
                debug_build_details_enabled(opt_))
            {
              build::print_build_info(
                  std::cout,
                  "Artifact cache skipped: no main executable artifact found");
            }

            std::string lastBinary;

            const auto exeOpt = resolve_main_executable(
                plan_.buildDir,
                plan_.userProjectDir,
                opt_.buildTarget,
                plan_.defaultTargetName);

            if (exeOpt)
            {
              lastBinary = exeOpt->string();

              write_project_build_metadata(
                  plan_.userProjectDir,
                  plan_.buildDir,
                  *exeOpt,
                  opt_.sanitizerMode);
            }

            const auto state =
                artifact_cache::ArtifactCache::make_build_state(
                    plan_.signature,
                    plan_.projectFingerprint,
                    projectArtifact.root.string(),
                    lastBinary,
                    opt_.buildTarget,
                    plan_.preset.name,
                    plan_.preset.buildType,
                    projectArtifact.target,
                    projectArtifact.compiler,
                    projectInputs);

            if (!artifact_cache::ArtifactCache::write_build_state(plan_.buildDir, state) &&
                !opt_.quiet)
            {
              hint("Warning: unable to write Vix build state");
            }

            if (!graph.save(graphPath) && !opt_.quiet)
              hint("Warning: unable to write Vix build graph");

            if (liveBuild)
              liveBuild->finish(0);

            if (!opt_.quiet)
            {
              build_print_phase_timings(
                  phaseTimings);

              print_graph_warnings_modern(
                  graphResult.output);

              /*
               * LiveBuild owns the compact presentation for the normal mode.
               * Verbose modes keep the historical detailed Vix build presentation.
               */
              if (!liveBuild)
              {
                if (!buildHeaderPrinted)
                  print_vix_build_header(
                      "Building",
                      opt_,
                      plan_);

                if (configuredThisRun)
                  print_vix_build_success(
                      "Configured");

                print_vix_build_success(
                    "Graph target: " +
                    graphResult.target);

                if (graphResult.dirtyCompileTasks == 0)
                {
                  print_vix_build_success(
                      "Up to date");
                }
                else
                {
                  print_vix_build_success(
                      "Compiled " +
                      std::to_string(
                          graphResult.dirtyCompileTasks) +
                      " dirty files");
                }

                print_vix_build_success(
                    "Done");
              }
            }

            return 0;
          }

          if (debug_build_details_enabled(opt_) && !opt_.quiet)
            hint("Graph target executor fallback: " + graphResult.output);
        }

        if (graph_executor_enabled(opt_) && can_use_graph_build(opt_, plan_, scan))
        {
          const int graphBuildCode =
              measurePhase(
                  "build",
                  [&]()
                  {
                    return run_graph_build(
                        graph,
                        graphPath,
                        opt_,
                        plan_,
                        projectArtifact,
                        projectInputs,
                        verboseMode,
                        liveBuild
                            ? &*liveBuild
                            : nullptr);
                  });

          if (opt_.explain &&
              !opt_.quiet)
          {
            build_print_phase_timings(
                phaseTimings);
          }

          if (liveBuild)
          {
            liveBuild->finish(
                graphBuildCode);
          }

          return graphBuildCode;
        }

        {
          const auto t0 =
              std::chrono::steady_clock::now();

          const auto argv =
              build::cmake_build_argv(
                  plan_,
                  opt_);
          const auto env = build::ninja_env(opt_, plan_);

          const bool showRawBuildOutput =
              rawBuildOutput;

          const bool progressOnly =
              !showRawBuildOutput &&
              watch_stdout_is_tty();

          const bool legacyBuildQuiet =
              opt_.quiet ||
              (liveBuild &&
               !showRawBuildOutput);

          const process::ExecResult r =
              measurePhase(
                  "build",
                  [&]()
                  {
                    return build::run_process_live_to_log(
                        argv,
                        env,
                        plan_.buildLog,
                        legacyBuildQuiet,
                        opt_.cmakeVerbose,
                        progressOnly,
                        liveBuild
                            ? liveBuild->observer()
                            : build::BuildOutputObserver{},
                        opt_.heartbeat);
                  });

          const auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();

          if (r.exitCode != 0)
          {
            out.discard();

            const int exitCode =
                (r.exitCode == 0)
                    ? 3
                    : r.exitCode;

            if (liveBuild)
            {
              liveBuild->finish(
                  exitCode);
            }

            const std::string log =
                util::read_text_file_or_empty(
                    plan_.buildLog);
            const bool handled =
                vix::cli::ErrorHandler::printBuildErrors(
                    log,
                    plan_.cmakeSourceDir / "CMakeLists.txt",
                    "Build failed");

            if (!opt_.quiet)
            {
              if (!handled)
                hint("run `vix build --log build` for the captured build output");

              print_debug_command_if_enabled(opt_, r);
            }
            return exitCode;
          }

          if (liveBuild)
          {
            liveBuild->finish(0);
          }

          if (!store_project_target_artifact(projectArtifact, opt_, plan_) &&
              !opt_.quiet &&
              debug_build_details_enabled(opt_))
          {
            build::print_build_info(
                std::cout,
                "Artifact cache skipped: no main executable artifact found");
          }
          std::string lastBinary;

          const auto exeOpt = resolve_main_executable(
              plan_.buildDir,
              plan_.userProjectDir,
              opt_.buildTarget,
              plan_.defaultTargetName);

          if (exeOpt)
          {
            lastBinary = exeOpt->string();

            write_project_build_metadata(
                plan_.userProjectDir,
                plan_.buildDir,
                *exeOpt,
                opt_.sanitizerMode);
          }

          const auto state =
              artifact_cache::ArtifactCache::make_build_state(
                  plan_.signature,
                  plan_.projectFingerprint,
                  projectArtifact.root.string(),
                  lastBinary,
                  opt_.buildTarget,
                  plan_.preset.name,
                  plan_.preset.buildType,
                  projectArtifact.target,
                  projectArtifact.compiler,
                  projectInputs);

          if (!artifact_cache::ArtifactCache::write_build_state(plan_.buildDir, state) &&
              !opt_.quiet)
          {
            hint("Warning: unable to write Vix build state");
          }

          if (!graph.save(graphPath) && !opt_.quiet)
            hint("Warning: unable to write Vix build graph");

          const std::string buildLog =
              util::read_text_file_or_empty(plan_.buildLog);

          if (!opt_.quiet)
            print_compiler_warnings_summary(buildLog);

          if (!opt_.quiet)
          {
            build_print_phase_timings(
                phaseTimings);

            if (!rawBuildOutput)
            {
              out.flush_to_stdout();

              if (!buildHeaderPrinted)
              {
                const std::optional<std::string> effectiveLauncher =
                    plan_.launcher ? plan_.launcher : std::optional<std::string>{"none"};
                build::print_build_header_full(
                    std::cout,
                    build::default_build_target_name(
                        opt_,
                        plan_),
                    display_build_profile(plan_),
                    verboseMode ? effectiveLauncher : std::nullopt,
                    verboseMode ? plan_.fastLinkerFlag : std::nullopt,
                    verboseMode
                        ? (opt_.jobs <= 0
                               ? build::default_jobs()
                               : opt_.jobs)
                        : 0);
              }

              const std::string profile =
                  (plan_.preset.buildType == "Release")
                      ? "release [optimized]"
                      : "dev [unoptimized + debuginfo]";

              build::print_build_done(
                  std::cout,
                  profile,
                  util::format_seconds(ms));
            }
          }
        }

        if (opt_.exportBin || !opt_.outPath.empty())
        {
          const auto exeOpt = resolve_main_executable(
              plan_.buildDir,
              plan_.userProjectDir,
              opt_.buildTarget,
              plan_.defaultTargetName);

          if (!exeOpt)
          {
            error("Unable to resolve the main executable to export.");
            hint("Use --build-target <name> if your project produces multiple executables.");
            hint("Run: vix build --build-target <target> -v");
            return 1;
          }

          fs::path dest;

          if (opt_.exportBin)
            dest = plan_.userProjectDir / exeOpt->filename();
          else
            dest = fs::absolute(fs::path(opt_.outPath));

          if (!export_built_binary(*exeOpt, dest, opt_.quiet))
            return 1;
        }

        out.flush_to_stdout();
        return 0;
      }

    private:
      int run_single_cpp_build();
      int run_watch();

    private:
      process::Options opt_;
      process::Plan plan_{};
    };

  } // namespace

  int run(const std::vector<std::string> &args)
  {
    int parseExit = 0;
    process::Options opt = parse_args_or_exit(args, parseExit);

    if (opt.exportBin && !opt.outPath.empty())
    {
      error("Options --bin and --out cannot be used together.");
      hint("Use either --bin or --out <path>.");
      return 2;
    }

    if (opt.watch && opt.warnings)
    {
      error("Options --watch and --warnings cannot be used together.");
      hint("--watch rebuilds continuously; --warnings reads the last completed build log.");
      return 2;
    }

    if (opt.watch && opt.report)
    {
      error("Options --watch and --report cannot be used together.");
      hint("Cloud build reports are only submitted for one-shot builds.");
      return 2;
    }

    if (parseExit == -2)
      return help();
    if (parseExit != 0)
      return parseExit;

    if (opt.listTargets)
    {
      std::map<std::string, std::string> detected;
      if (const char *pathEnv = std::getenv("PATH"); pathEnv && *pathEnv)
      {
#ifdef _WIN32
        const char separator = ';';
#else
        const char separator = ':';
#endif
        std::istringstream paths(pathEnv);
        std::string directory;
        while (std::getline(paths, directory, separator))
        {
          std::error_code ec;
          for (const auto &entry : fs::directory_iterator(directory, ec))
          {
            if (ec || !entry.is_regular_file(ec))
              continue;
            const std::string name = entry.path().filename().string();
            const std::string suffix = "-g++";
            if (name.size() > suffix.size() && name.rfind(suffix) == name.size() - suffix.size())
              detected.emplace(name.substr(0, name.size() - suffix.size()), name);
          }
        }
      }

      info("Available targets");
      step("native");
      if (opt.verbose)
        step("  status: native");

      for (const auto &[target, compiler] : detected)
      {
        step(target + "  available");
        if (opt.verbose)
        {
          step("  status: available");
          step("  compiler: " + compiler);
        }
      }
      step("Use: vix build --target <target>");
      return 0;
    }

    if (!build::resolve_builtin_preset(opt.preset))
    {
      error("Unknown preset: " + opt.preset);
      hint("Available presets: dev, dev-ninja, release");
      return 2;
    }

    process::Options reportOpt = opt;
    const auto reportStart = std::chrono::steady_clock::now();

    BuildCommand cmd(std::move(opt));
    const int buildCode = cmd.run();

    if (!reportOpt.report)
      return buildCode;

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - reportStart)
                                .count();

    CloudBuildReport report;
    report.status = buildCode == 0 ? "success" : "failed";
    report.target = reportOpt.buildTarget.empty() ? "default" : reportOpt.buildTarget;
    report.profile = reportOpt.preset;
    report.toolchain = reportOpt.targetTriple;
    report.summary_message = buildCode == 0 ? "Build completed" : "Build failed";
    report.duration_ms = durationMs < 0 ? 0 : static_cast<std::int64_t>(durationMs);
    report.warnings_count = 0;
    report.errors_count = buildCode == 0 ? 0 : 1;

    std::string cloudMessage;
    const bool sent = CloudCommand::submit_build_report(report, cloudMessage);

    if (buildCode == 0)
      std::cout << "Build succeeded.\n";
    else
      std::cout << "Build failed.\n";

    if (sent)
      std::cout << "Build report sent to Softadastra Cloud.\n";
    else
      std::cout << "Could not send build report to Softadastra Cloud: "
                << (cloudMessage.empty() ? "Cloud request failed." : cloudMessage)
                << "\n";

    return buildCode;
  }

  int BuildCommand::run_single_cpp_build()
  {
    if (opt_.cppFile.empty())
    {
      error("No C++ source file provided.");
      return 1;
    }

    if (!fs::exists(opt_.cppFile))
    {
      error("Source file not found: " + opt_.cppFile.string());
      return 1;
    }

    run_detail::Options runOpt{};
    runOpt.singleCpp = true;
    runOpt.cppFile = fs::absolute(opt_.cppFile);

    runOpt.preset = opt_.preset;
    runOpt.dir = opt_.dir;
    runOpt.jobs = opt_.jobs;
    runOpt.clean = opt_.clean;

    runOpt.quiet = opt_.quiet;
    runOpt.verbose = opt_.verbose;

    runOpt.withSqlite = opt_.withSqlite;
    runOpt.withMySql = opt_.withMySql;

    runOpt.enableSanitizers = false;
    runOpt.enableUbsanOnly = false;

    runOpt.forceServerLike = false;
    runOpt.forceScriptLike = true;

    runOpt.watch = false;
    runOpt.timeoutSec = 0;
    runOpt.cwd.clear();

    runOpt.runArgs.clear();
    runOpt.runEnv.clear();
    runOpt.scriptFlags = opt_.cmakeArgs;

    fs::path exePath;
    const int code = run_detail::build_script_executable(runOpt, exePath);
    if (code != 0)
      return code;

    if (exePath.empty() || !fs::exists(exePath))
    {
      error("Built executable was not produced.");
      return 1;
    }

    fs::path dest;

    if (!opt_.outPath.empty())
    {
      dest = fs::absolute(fs::path(opt_.outPath));
    }
    else
    {
      dest = fs::current_path() / exePath.filename();
    }

    return export_built_binary(exePath, dest, opt_.quiet) ? 0 : 1;
  }

  int BuildCommand::run_watch()
  {
    const bool structuredWatchOutput = !opt_.quiet;
    const bool verboseWatchDetails = opt_.verbose && structuredWatchOutput;
    const bool rawBuildOutput = opt_.cmakeVerbose;

    if (opt_.singleCpp)
    {
      process::Options buildOpt = opt_;
      buildOpt.watch = false;

      BuildCommand initial(buildOpt);
      const auto initialT0 = std::chrono::steady_clock::now();
      WatchCapturedRun initialRun =
          watch_run_capturing_stderr(
              structuredWatchOutput && !rawBuildOutput,
              [&]()
              {
                return initial.run_single_cpp_build();
              });
      int lastCode = initialRun.code;
      const auto initialMs =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - initialT0)
              .count();

      const fs::path source = fs::absolute(opt_.cppFile).lexically_normal();
      const fs::path root = source.parent_path();
      WatchDisplayContext display;
      display.projectDir = root;
      display.quiet = opt_.quiet;
      display.verbose = opt_.verbose;

      vix::engine::watch::Options watchOptions;
      watchOptions.root = root;
      watchOptions.debounce = std::chrono::milliseconds(25);
      watchOptions.maxBatchWindow = std::chrono::milliseconds(100);

      vix::engine::watch::FileWatcher watcher(watchOptions);
      const auto started = watcher.start();
      if (!started.ok)
      {
        error("Unable to start build watcher.");
        hint(started.error);
        return 1;
      }

      g_watch_stop_requested = 0;
      using SignalHandler = void (*)(int);
      SignalHandler oldInt = std::signal(SIGINT, on_watch_signal);
      SignalHandler oldTerm = std::signal(SIGTERM, on_watch_signal);

      if (structuredWatchOutput)
      {
        if (lastCode == 0)
        {
          if (verboseWatchDetails)
          {
            watch_print_header(display, source.stem().string());
            watch_print_session_metadata(
                display,
                watcher.backend(),
                root,
                source.stem().string(),
                buildOpt.jobs <= 0 ? build::default_jobs() : buildOpt.jobs,
                std::nullopt,
                std::nullopt);
            watch_print_initial_done(display, initialMs);
            watch_print_waiting(display);
          }
          else
          {
            watch_print_ready(display, source.stem().string(), initialMs);
          }
        }
        else
          watch_print_initial_failed(
              display,
              source.stem().string(),
              initialRun.diagnostics);
      }

      if (structuredWatchOutput && !rawBuildOutput)
        buildOpt.quiet = true;

      WatchContentFingerprints contentFingerprints(root, watchOptions.ignoredRoots);
      contentFingerprints.seed();

      while (!g_watch_stop_requested)
      {
        auto batchOpt = watcher.wait_for_batch(std::chrono::milliseconds(100));
        if (!batchOpt || batchOpt->empty())
          continue;

        *batchOpt = contentFingerprints.filter(*batchOpt);
        if (batchOpt->empty())
          continue;

        bool relevant = batchOpt->overflowed;
        for (const auto &event : batchOpt->events)
        {
          if (event.path.lexically_normal() == source)
            relevant = true;
        }

        if (!relevant)
          continue;

        vix::engine::watch::Batch displayBatch;
        displayBatch.events.push_back(
            {vix::engine::watch::EventKind::Modified, source, {}, false});

        const auto t0 = std::chrono::steady_clock::now();
        WatchProgressLine progress(
            display,
            displayBatch,
            WatchDisplayAction::Rebuilt,
            false);
        BuildCommand rebuild(buildOpt);
        WatchCapturedRun run =
            watch_run_capturing_stderr(
                structuredWatchOutput && !rawBuildOutput,
                [&]()
                {
                  return rebuild.run_single_cpp_build();
                });
        lastCode = run.code;
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0)
                .count();

        progress.stop();

        if (g_watch_stop_requested)
          break;

        if (structuredWatchOutput)
        {
          if (lastCode == 0)
            watch_print_completed(
                display,
                displayBatch,
                WatchDisplayAction::Rebuilt,
                false,
                ms);
          else
            watch_print_failed(
                display,
                displayBatch,
                WatchDisplayAction::Rebuilt,
                false,
                run.diagnostics);
        }
      }

      watcher.stop();
      std::signal(SIGINT, oldInt);
      std::signal(SIGTERM, oldTerm);

      if (structuredWatchOutput)
        watch_finish_terminal(display);

      return 130;
    }

    {
      fs::path base = fs::current_path();
      if (!opt_.dir.empty())
        base = fs::absolute(fs::path(opt_.dir));

      const app::AppProjectResolveResult project =
          app::resolve_app_project(base);

      if (project.success() &&
          project.kind == app::AppProjectKind::VixApp)
      {
        const app::AppManifestLoadResult loadResult =
            app::load_app_manifest(project.appManifestPath);

        if (!loadResult.success())
        {
          error("Failed to load vix.app.");
          hint(loadResult.error);
          return 1;
        }

        if (!opt_.warnings &&
            can_use_native_vix_app_build(opt_, loadResult.manifest))
        {
          app::AppManifest activeManifest = loadResult.manifest;
          process::Options buildOpt = opt_;
          buildOpt.watch = false;

          const auto initialT0 = std::chrono::steady_clock::now();
          WatchCapturedRun initialRun =
              watch_run_capturing_stderr(
                  structuredWatchOutput && !rawBuildOutput,
                  [&]()
                  {
                    return run_native_vix_app_build(
                        buildOpt,
                        project.userProjectDir,
                        activeManifest,
                        std::chrono::steady_clock::now(),
                        false);
                  });
          int lastCode = initialRun.code;
          const auto initialMs =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - initialT0)
                  .count();

          NativeVixAppBuildSession nativeSession;
          int prepareExit = 0;
          if (!prepare_native_vix_app_build_session(
                  buildOpt,
                  project.userProjectDir,
                  activeManifest,
                  nativeSession,
                  prepareExit))
          {
            return prepareExit;
          }

          vix::engine::watch::Options watchOptions;
          watchOptions.root = project.userProjectDir;
          watchOptions.debounce = std::chrono::milliseconds(25);
          watchOptions.maxBatchWindow = std::chrono::milliseconds(100);
          watchOptions.ignoredRoots.push_back(
              native_vix_app_build_dir(project.userProjectDir, buildOpt));
          watchOptions.ignoredRoots.push_back(project.userProjectDir / ".git");
          watchOptions.ignoredRoots.push_back(project.userProjectDir / ".vix");

          if (nativeSession.outputBinary.has_parent_path())
          {
            const fs::path outputParent =
                nativeSession.outputBinary.parent_path().lexically_normal();
            const fs::path projectRoot =
                project.userProjectDir.lexically_normal();

            if (outputParent != projectRoot)
              watchOptions.ignoredRoots.push_back(outputParent);
          }

          vix::engine::watch::FileWatcher watcher(watchOptions);
          const auto started = watcher.start();
          if (!started.ok)
          {
            error("Unable to start build watcher.");
            hint(started.error);
            return 1;
          }

          g_watch_stop_requested = 0;
          using SignalHandler = void (*)(int);
          SignalHandler oldInt = std::signal(SIGINT, on_watch_signal);
          SignalHandler oldTerm = std::signal(SIGTERM, on_watch_signal);

          WatchDisplayContext display;
          display.projectDir = project.userProjectDir;
          display.quiet = opt_.quiet;
          display.verbose = opt_.verbose;

          if (structuredWatchOutput)
          {
            if (lastCode == 0)
            {
              const std::string targetName =
                  activeManifest.name.empty()
                      ? std::string("vix.app")
                      : activeManifest.name;

              if (verboseWatchDetails)
              {
                watch_print_header(display, targetName);
                watch_print_session_metadata(
                    display,
                    watcher.backend(),
                    nativeSession.plan.buildDir,
                    targetName,
                    buildOpt.jobs <= 0 ? build::default_jobs() : buildOpt.jobs,
                    nativeSession.plan.launcher,
                    nativeSession.plan.fastLinkerFlag);
                watch_print_initial_done(display, initialMs);
                watch_print_waiting(display);
              }
              else
              {
                watch_print_ready(display, targetName, initialMs);
              }
            }
            else
              watch_print_initial_failed(
                  display,
                  activeManifest.name.empty()
                      ? std::string("vix.app")
                      : activeManifest.name,
                  initialRun.diagnostics);
          }

          if (structuredWatchOutput && !rawBuildOutput)
            buildOpt.quiet = true;

          WatchContentFingerprints contentFingerprints(
              project.userProjectDir,
              watchOptions.ignoredRoots);
          contentFingerprints.seed();

          while (!g_watch_stop_requested)
          {
            auto batchOpt = watcher.wait_for_batch(std::chrono::milliseconds(100));
            if (!batchOpt || batchOpt->empty())
              continue;

            *batchOpt = contentFingerprints.filter(*batchOpt);
            if (batchOpt->empty())
              continue;

            bool relevant = batchOpt->overflowed;
            for (const auto &event : batchOpt->events)
            {
              const std::string ext = event.path.extension().string();
              const std::string name = event.path.filename().string();
              if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" ||
                  ext == ".c" || ext == ".hpp" || ext == ".hh" ||
                  ext == ".hxx" || ext == ".h" || name == "vix.app")
              {
                relevant = true;
              }
            }

            if (!relevant)
              continue;

            const WatchDisplayAction action =
                watch_batch_has_configuration_path(*batchOpt)
                    ? WatchDisplayAction::Reconfigured
                    : WatchDisplayAction::Rebuilt;

            const std::vector<std::string> sourceTaskIds =
                native_vix_app_task_ids_for_batch(nativeSession, *batchOpt);

            bool sourceOnlyChange =
                action == WatchDisplayAction::Rebuilt &&
                !batchOpt->overflowed &&
                !sourceTaskIds.empty();

            if (sourceOnlyChange)
            {
              for (const auto &event : batchOpt->events)
              {
                if (!native_vix_app_is_source_path(event.path))
                {
                  sourceOnlyChange = false;
                  break;
                }
              }
            }

            const auto t0 = std::chrono::steady_clock::now();
            WatchProgressLine progress(
                display,
                *batchOpt,
                action,
                false,
                sourceOnlyChange ? std::string() : std::string("full refresh"));
            const std::string finalSubject =
                sourceOnlyChange
                    ? compile_task_summary_subject(
                          nativeSession.graph,
                          sourceTaskIds,
                          project.userProjectDir)
                    : std::string();
            WatchCapturedRun run =
                watch_run_capturing_stderr(
                    structuredWatchOutput && !rawBuildOutput,
                    [&]()
                    {
                      if (sourceOnlyChange)
                      {
                        const int compileCode =
                            run_native_vix_app_tasks(
                                buildOpt,
                                nativeSession,
                                sourceTaskIds,
                                &progress);

                        if (compileCode != 0)
                          return compileCode;

                        return link_native_vix_app_build(
                            buildOpt,
                            project.userProjectDir,
                            activeManifest,
                            nativeSession,
                            &progress);
                      }

                      if (action == WatchDisplayAction::Reconfigured)
                      {
                        const app::AppManifestLoadResult reloadResult =
                            app::load_app_manifest(project.appManifestPath);

                        if (!reloadResult.success())
                        {
                          error("Failed to load vix.app.");
                          hint(reloadResult.error);
                          return 1;
                        }

                        activeManifest = reloadResult.manifest;

                        if (!can_use_native_vix_app_build(buildOpt, activeManifest))
                        {
                          BuildCommand fallback(buildOpt);
                          return fallback.run();
                        }
                      }

                      int refreshPrepareExit = 0;
                      if (!prepare_native_vix_app_build_session(
                              buildOpt,
                              project.userProjectDir,
                              activeManifest,
                              nativeSession,
                              refreshPrepareExit))
                      {
                        return refreshPrepareExit;
                      }

                      const int compileCode =
                          run_native_vix_app_tasks(
                              buildOpt,
                              nativeSession,
                              {},
                              &progress,
                              "full refresh");

                      if (compileCode != 0)
                        return compileCode;

                      return link_native_vix_app_build(
                          buildOpt,
                          project.userProjectDir,
                          activeManifest,
                          nativeSession,
                          &progress,
                          "full refresh");
                    });
            lastCode = run.code;
            const auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0)
                    .count();

            progress.stop();

            if (g_watch_stop_requested)
              break;

            if (structuredWatchOutput)
            {
              if (lastCode == 0)
                watch_print_completed(
                    display,
                    *batchOpt,
                    action,
                    false,
                    ms,
                    sourceOnlyChange ? std::string() : std::string("full refresh"),
                    finalSubject);
              else
                watch_print_failed(
                    display,
                    *batchOpt,
                    action,
                    false,
                    run.diagnostics);
            }
          }

          watcher.stop();
          std::signal(SIGINT, oldInt);
          std::signal(SIGTERM, oldTerm);

          if (structuredWatchOutput)
            watch_finish_terminal(display);

          return 130;
        }
      }
    }

    process::Options initialOpt = opt_;
    initialOpt.watch = false;

    BuildCommand initial(std::move(initialOpt));
    const auto initialT0 = std::chrono::steady_clock::now();
    WatchCapturedRun initialRun =
        watch_run_capturing_stderr(
            structuredWatchOutput && !rawBuildOutput,
            [&]()
            {
              return initial.run();
            });
    int lastCode = initialRun.code;
    const auto initialMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - initialT0)
            .count();
    plan_ = initial.plan_;

    if (plan_.userProjectDir.empty())
    {
      if (lastCode != 0)
        return lastCode;

      error("Unable to start watch mode: project plan was not available.");
      return 1;
    }

    process::Options sessionOpt = opt_;
    sessionOpt.watch = false;
    sessionOpt.clean = false;
    if (structuredWatchOutput && !rawBuildOutput)
      sessionOpt.quiet = true;

    const fs::path graphPath =
        build::BuildGraph::default_graph_path(plan_.buildDir);
    std::optional<build::BuildGraph> graph =
        build::BuildGraph::load(graphPath);

    vix::engine::watch::Options watchOptions;
    watchOptions.root = plan_.userProjectDir;
    watchOptions.debounce = std::chrono::milliseconds(25);
    watchOptions.maxBatchWindow = std::chrono::milliseconds(100);
    watchOptions.ignoredRoots.push_back(plan_.buildDir);
    watchOptions.ignoredRoots.push_back(plan_.userProjectDir / ".git");
    watchOptions.ignoredRoots.push_back(plan_.userProjectDir / ".hg");
    watchOptions.ignoredRoots.push_back(plan_.userProjectDir / ".svn");
    watchOptions.ignoredRoots.push_back(plan_.userProjectDir / ".vix");
    watchOptions.ignoredRoots.push_back(plan_.userProjectDir / "node_modules");
    watchOptions.ignoredRoots.push_back(plan_.userProjectDir / ".cache");

    if (!sessionOpt.outPath.empty())
    {
      const fs::path outPath = fs::absolute(fs::path(sessionOpt.outPath));
      if (outPath.has_parent_path())
        watchOptions.ignoredRoots.push_back(outPath);
    }

    vix::engine::watch::FileWatcher watcher(watchOptions);
    const auto watchStart = watcher.start();
    if (!watchStart.ok)
    {
      error("Unable to start build watcher.");
      hint(watchStart.error);
      return 1;
    }

    g_watch_stop_requested = 0;
    using SignalHandler = void (*)(int);
    SignalHandler oldInt = std::signal(SIGINT, on_watch_signal);
    SignalHandler oldTerm = std::signal(SIGTERM, on_watch_signal);

    WatchDisplayContext watchDisplay;
    watchDisplay.projectDir = plan_.userProjectDir;
    watchDisplay.quiet = opt_.quiet;
    watchDisplay.verbose = opt_.verbose;

    if (structuredWatchOutput)
    {
      if (lastCode == 0)
      {
        const std::string targetName =
            build::default_build_target_name(sessionOpt, plan_);

        if (verboseWatchDetails)
        {
          watch_print_header(watchDisplay, targetName);
          watch_print_session_metadata(
              watchDisplay,
              watcher.backend(),
              plan_.buildDir,
              targetName,
              sessionOpt.jobs <= 0 ? build::default_jobs() : sessionOpt.jobs,
              plan_.launcher,
              plan_.fastLinkerFlag);
          watch_print_initial_done(watchDisplay, initialMs);
          watch_print_waiting(watchDisplay);
        }
        else
        {
          watch_print_ready(watchDisplay, targetName, initialMs);
        }
      }
      else
      {
        watch_print_initial_failed(
            watchDisplay,
            build::default_build_target_name(sessionOpt, plan_),
            initialRun.diagnostics);
      }
    }

    WatchContentFingerprints contentFingerprints(
        plan_.userProjectDir,
        watchOptions.ignoredRoots);
    contentFingerprints.seed();

    auto restore_signals =
        [&]()
    {
      std::signal(SIGINT, oldInt);
      std::signal(SIGTERM, oldTerm);
    };

    auto drain_pending_events =
        [&]() -> std::optional<vix::engine::watch::Batch>
    {
      vix::engine::watch::Batch drained;

      while (!g_watch_stop_requested)
      {
        auto next =
            watcher.wait_for_batch(std::chrono::milliseconds(0));
        if (!next || next->empty())
          break;

        drained.overflowed = drained.overflowed || next->overflowed;
        drained.events.insert(
            drained.events.end(),
            next->events.begin(),
            next->events.end());
      }

      drained = contentFingerprints.filter(drained);

      if (drained.empty())
        return std::nullopt;

      return drained;
    };

    auto run_full_refresh =
        [&]() -> WatchCapturedRun
    {
      BuildCommand cmd(sessionOpt);
      WatchCapturedRun run =
          watch_run_capturing_stderr(
              structuredWatchOutput && !rawBuildOutput,
              [&]()
              {
                return cmd.run();
              });
      if (!cmd.plan_.userProjectDir.empty())
        plan_ = cmd.plan_;
      graph = build::BuildGraph::load(
          build::BuildGraph::default_graph_path(plan_.buildDir));
      watchDisplay.projectDir = plan_.userProjectDir;
      return run;
    };

    auto run_incremental =
        [&](build::BuildGraph &currentGraph,
            const build::BuildGraphInvalidationResult &invalidation,
            const vix::engine::watch::Batch &batch) -> int
    {
      const auto t0 = std::chrono::steady_clock::now();
      WatchProgressLine progress(
          watchDisplay,
          batch,
          WatchDisplayAction::Rebuilt,
          false);
      std::mutex observedCompileTasksMutex;
      std::vector<std::string> observedCompileTaskIds;
      std::set<std::string> observedCompileTaskSet;

      if (sessionOpt.explain && structuredWatchOutput)
      {
        watch_print_explain_affected_tasks(
            watchDisplay,
            invalidation.affectedTasks);

        if (!sessionOpt.quiet)
        {
          for (const std::string &taskId : invalidation.dirtyTaskIds)
            step(taskId);
        }
      }

      build::BuildGraphExecutorOptions executorOptions;
      executorOptions.buildDir = plan_.buildDir;
      executorOptions.target =
          sessionOpt.buildTarget.empty()
              ? std::string("all")
              : build::default_graph_target_name(sessionOpt, plan_);
      executorOptions.jobs = sessionOpt.jobs;
      executorOptions.allowNinjaFallback = true;

      build::BuildGraphExecutorDependencies executorDependencies;
      executorDependencies.executeCompileTask =
          [](build::BuildTask &task)
      {
        return build::execute_build_task_process(task);
      };
      executorDependencies.executeNinjaTarget =
          [&](const build::BuildGraphExecutorNinjaRequest &request)
      {
        return build::execute_graph_ninja_target(
            request,
            !sessionOpt.cmakeVerbose);
      };
      executorDependencies.onEvent =
          [&](const build::BuildGraphExecutorEvent &event)
      {
        if (event.kind == build::BuildGraphExecutorEventKind::CompilingTask &&
            !event.taskId.empty())
        {
          {
            std::lock_guard<std::mutex> lock(observedCompileTasksMutex);
            if (observedCompileTaskSet.insert(event.taskId).second)
              observedCompileTaskIds.push_back(event.taskId);
          }

          progress.update(
              "Building",
              compile_task_subject_for_id(
                  currentGraph,
                  event.taskId,
                  plan_.userProjectDir));
        }
        else if (event.kind == build::BuildGraphExecutorEventKind::RunningNinja)
        {
          progress.update(
              "Linking",
              event.target.empty()
                  ? build::default_build_target_name(sessionOpt, plan_)
                  : event.target);
        }

        if (sessionOpt.cmakeVerbose)
        {
          build::render_graph_debug_event(
              event,
              false,
              true);
        }
      };

      build::BuildGraphExecutor executor(
          executorOptions,
          std::move(executorDependencies));

      const build::BuildGraphExecutorResult result =
          executor.run_target(currentGraph);

      const auto ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - t0)
              .count();

      progress.stop();

      if (g_watch_stop_requested)
        return 130;

      if (!result.ok)
      {
        if (structuredWatchOutput)
        {
          watch_print_failed(
              watchDisplay,
              batch,
              WatchDisplayAction::Rebuilt,
              false,
              result.output);
        }
        else if (!sessionOpt.quiet)
        {
          error("Rebuild failed in " + util::format_seconds(ms));
          if (!result.output.empty())
            std::cerr << result.output;
          hint("Waiting for changes...");
        }
        return result.exitCode == 0 ? 1 : result.exitCode;
      }

      if (!currentGraph.save(
              build::BuildGraph::default_graph_path(plan_.buildDir)) &&
          !sessionOpt.quiet)
      {
        hint("Warning: unable to write Vix build graph");
      }

      if (sessionOpt.exportBin || !sessionOpt.outPath.empty())
      {
        const auto exeOpt = resolve_main_executable(
            plan_.buildDir,
            plan_.userProjectDir,
            sessionOpt.buildTarget,
            plan_.defaultTargetName);

        if (exeOpt)
        {
          fs::path dest;
          if (sessionOpt.exportBin)
            dest = plan_.userProjectDir / exeOpt->filename();
          else
            dest = fs::absolute(fs::path(sessionOpt.outPath));

          if (!export_built_binary(*exeOpt, dest, sessionOpt.quiet))
            return 1;
        }
      }

      if (structuredWatchOutput)
      {
        std::vector<std::string> finalTaskIds;

        {
          std::lock_guard<std::mutex> lock(observedCompileTasksMutex);
          finalTaskIds = observedCompileTaskIds;
        }

        const std::string finalSubject =
            finalTaskIds.empty()
                ? std::string()
                : compile_task_summary_subject(
                      currentGraph,
                      finalTaskIds,
                      plan_.userProjectDir);

        watch_print_completed(
            watchDisplay,
            batch,
            WatchDisplayAction::Rebuilt,
            false,
            ms,
            {},
            finalSubject);
      }
      return 0;
    };

    std::optional<vix::engine::watch::Batch> pendingBatch;

    while (!g_watch_stop_requested)
    {
      std::optional<vix::engine::watch::Batch> batchOpt;

      if (pendingBatch)
      {
        batchOpt = std::move(pendingBatch);
        pendingBatch.reset();
      }
      else
      {
        batchOpt =
            watcher.wait_for_batch(std::chrono::milliseconds(100));
      }

      if (!batchOpt || batchOpt->empty())
        continue;

      *batchOpt = contentFingerprints.filter(*batchOpt);
      if (batchOpt->empty())
        continue;

      const auto &batch = *batchOpt;

      if (!graph)
      {
        const WatchDisplayAction action =
            watch_batch_has_configuration_path(batch)
                ? WatchDisplayAction::Reconfigured
                : WatchDisplayAction::Rebuilt;

        const auto t0 = std::chrono::steady_clock::now();
        WatchProgressLine progress(
            watchDisplay,
            batch,
            action,
            false,
            "full refresh");
        WatchCapturedRun run = run_full_refresh();
        lastCode = run.code;

        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0)
                .count();

        progress.stop();

        if (g_watch_stop_requested)
          break;
        if (structuredWatchOutput)
        {
          if (lastCode == 0)
            watch_print_completed(
                watchDisplay,
                batch,
                action,
                false,
                ms,
                "full refresh");
          else
            watch_print_failed(
                watchDisplay,
                batch,
                action,
                false,
                run.diagnostics);
        }
        pendingBatch = drain_pending_events();
        continue;
      }

      build::BuildGraphInvalidationResult invalidation =
          graph->invalidate_paths(batch.events);

      if (!invalidation.relevant)
        continue;

      if (!batch.overflowed &&
          !invalidation.structuralChange &&
          invalidation.changedNodes == 0 &&
          invalidation.affectedTasks == 0)
      {
        continue;
      }

      if (batch.overflowed || invalidation.structuralChange)
      {
        const WatchDisplayAction action =
            watch_batch_has_configuration_path(batch)
                ? WatchDisplayAction::Reconfigured
                : WatchDisplayAction::Rebuilt;

        if (sessionOpt.explain && structuredWatchOutput)
          watch_print_explain_affected_tasks(
              watchDisplay,
              invalidation.affectedTasks);

        const auto t0 = std::chrono::steady_clock::now();
        WatchProgressLine progress(
            watchDisplay,
            batch,
            action,
            false,
            "full refresh");
        WatchCapturedRun run = run_full_refresh();
        lastCode = run.code;

        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0)
                .count();

        progress.stop();

        if (g_watch_stop_requested)
          break;
        if (structuredWatchOutput)
        {
          if (lastCode == 0)
            watch_print_completed(
                watchDisplay,
                batch,
                action,
                false,
                ms,
                "full refresh");
          else
            watch_print_failed(
                watchDisplay,
                batch,
                action,
                false,
                run.diagnostics);
        }
        pendingBatch = drain_pending_events();
        continue;
      }

      lastCode = run_incremental(*graph, invalidation, batch);

      if (g_watch_stop_requested)
        break;

      pendingBatch = drain_pending_events();
    }

    watcher.stop();
    restore_signals();

    if (structuredWatchOutput)
      watch_finish_terminal(watchDisplay);

    (void)lastCode;
    return 130;
  }

  int help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix build [source.cpp] [options] -- [cmake args...]\n\n";

    out << "Description:\n";
    out << "  Configure and build a C++ project with Vix.\n";
    out << "  Works with CMake projects, vix.app projects, and single C++ files.\n\n";

    out << "Project:\n";
    out << "  [source.cpp]              Build one C++ source file directly\n";
    out << "  -d, --dir <path>          Project directory\n";
    out << "  --dir=<path>              Same as --dir <path>\n\n";

    out << "Build:\n";
    out << "  --preset <name>           Use a preset: dev, dev-ninja, release\n";
    out << "  --preset=<name>           Same as --preset <name>\n";
    out << "  --build-target <name>     Build a specific CMake target\n";
    out << "  --build-target=<name>     Same as --build-target <name>\n";
    out << "  -j, --jobs <n>            Number of parallel build jobs\n";
    out << "  --jobs=<n>                Same as --jobs <n>\n";
    out << "  --clean                   Remove local build directories and configure again\n";
    out << "  --watch                   Watch project files and rebuild incrementally\n";
    out << "  --fast                    Use fast no-op detection when possible\n";
    out << "  --explain                 Explain why files or targets rebuild\n";
    out << "  --warnings                Show warnings from the last build log\n";
    out << "  --warning-check           Build with strong compiler warnings enabled\n";
    out << "  --sanitize                Build with AddressSanitizer and UndefinedBehaviorSanitizer\n";
    out << "  --sanitize=<mode>         Sanitizer: address, undefined, address,undefined, thread\n";
    out << "  --san                     Alias for --sanitize\n";
    out << "  --asan                    Alias for --sanitize=address\n";
    out << "  --ubsan                   Alias for --sanitize=undefined\n";
    out << "  --tsan                    Alias for --sanitize=thread\n";
    out << "  --report                  Submit a Softadastra Cloud build report\n";
    out << "  --page <n>                Warning page to display with --warnings, default: 1\n";
    out << "  --limit <n>               Warnings per page with --warnings, default: 10\n";
    out << "  --no-cache                Disable Vix cache shortcuts\n";
    out << "  --no-status               Disable Ninja progress status\n";
    out << "  --no-up-to-date           Disable Ninja dry-run up-to-date detection\n\n";

    out << "Output:\n";
    out << "  --bin                     Export the built executable to the project root\n";
    out << "  --out <path>              Export the built executable to a specific path\n";
    out << "  --out=<path>              Same as --out <path>\n\n";

    out << "Tooling:\n";
    out << "  --launcher <mode>         Compiler launcher: auto, none, sccache, ccache\n";
    out << "  --launcher=<mode>         Same as --launcher <mode>\n";
    out << "  --linker <mode>           Linker mode: auto, default, mold, lld\n";
    out << "  --linker=<mode>           Same as --linker <mode>\n\n";

    out << "Platform:\n";
    out << "  --target <triple>         Build for a target platform\n";
    out << "  --target native           Build for the current platform (default)\n";
    out << "  --target=<triple>         Same as --target <triple>\n";
    out << "  --sysroot <path>          Sysroot for the target toolchain (mainly cross builds)\n";
    out << "  --sysroot=<path>          Same as --sysroot <path>\n";
    out << "  --targets                 List detected targets and toolchains\n\n";

    out << "Linking and dependencies:\n";
    out << "  --static                  Request static linking\n";
    out << "  --with-sqlite             Enable SQLite support\n";
    out << "  --with-mysql              Enable MySQL support\n\n";
    out << "Managed SDK:\n";
    out << "  --managed-sdk             Resolve Vix dependencies from installed managed SDK profiles\n\n";

    out << "Diagnostics:\n";
    out << "  -v, --verbose             Show additional useful build information\n";
    out << "  --debug                   Show internal Vix build diagnostics\n";
    out << "  --debug-log <scope>       Debug cache, graph, configure, process, toolchain, or all\n";
    out << "  --log [path]              Show the current build log or a log file/directory\n";
    out << "  --cmake-verbose           Stream raw CMake, Ninja and compiler output\n";
    out << "  -q, --quiet               Minimal output\n";
    out << "  -h, --help                Show this help\n\n";

    out << "Advanced:\n";
    out << "  --graph-executor <mode>   Graph executor: auto, on, off\n";
    out << "  --heartbeat               Show progress heartbeat when a build is silent\n";
    out << "  --no-heartbeat            Disable the progress heartbeat\n\n";

    out << "CMake passthrough:\n";
    out << "  -- [cmake args...]        Pass extra arguments to CMake configure\n\n";

    out << "Examples:\n";
    out << "  vix build\n";
    out << "  vix build -v\n";
    out << "  vix build --fast\n";
    out << "  vix build --report\n";
    out << "  vix build --clean\n";
    out << "  vix build --watch\n";
    out << "  vix build --explain\n";
    out << "  vix build --warnings\n";
    out << "  vix build --warning-check --build-target all -v --clean\n";
    out << "  vix build --sanitize\n";
    out << "  vix build --sanitize=address\n";
    out << "  vix build --sanitize=undefined\n";
    out << "  vix build --sanitize=thread\n";
    out << "  vix build --warnings --page 2\n";
    out << "  vix build --warnings --limit 50\n";
    out << "  vix build --warnings --page 3 --limit 20\n";
    out << "  vix build --preset release\n";
    out << "  vix build --preset=release\n";
    out << "  vix build --build-target all\n";
    out << "  vix build --build-target vix -v\n";
    out << "  vix build --build-target=vix\n";
    out << "  vix build -j 8\n";
    out << "  vix build --jobs=8\n";
    out << "  vix build --launcher ccache --linker mold\n";
    out << "  vix build --launcher=ccache --linker=mold\n";
    out << "  vix build --with-sqlite\n";
    out << "  vix build --with-mysql\n";
    out << "  vix build --preset release --static\n";
    out << "  vix build --target aarch64-linux-gnu\n";
    out << "  vix build --target native\n";
    out << "  vix build --target=aarch64-linux-gnu\n";
    out << "  vix build --sysroot /opt/sysroot\n";
    out << "  vix build --targets\n";
    out << "  vix build --bin\n";
    out << "  vix build --out dist/app\n";
    out << "  vix build --out=dist/app\n";
    out << "  vix build main.cpp\n";
    out << "  vix build main.cpp --bin\n";
    out << "  vix build main.cpp --out app\n";
    out << "  vix build main.cpp --with-sqlite --out app\n";
    out << "  vix build main.cpp --target x86_64-windows-gnu --out app.exe\n";
    out << "  vix build --linker lld -- -DVIX_SYNC_BUILD_TESTS=ON\n";
    out << "  vix build --debug\n";
    out << "  vix build --log\n";
    out << "  vix build --log ./build/\n";
    out << "  vix build --log build-ninja/build.log\n";
    out << "  vix build --target aarch64-linux-gnu --sysroot /opt/sysroots/aarch64\n\n";

    return 0;
  }

} // namespace vix::commands::BuildCommand
