/**
 *
 *  @file RunCommand.cpp
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
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/commands/DiagnosticMode.hpp>
#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/cli/commands/run/RunDetail.hpp>
#include <vix/cli/commands/replay/ReplayCapture.hpp>
#include <vix/cli/commands/replay/ReplayRecorder.hpp>
#include <vix/cli/errors/RawLogDetectors.hpp>
#include <vix/cli/manifest/RunManifestMerge.hpp>
#include <vix/cli/manifest/VixManifest.hpp>
#include <vix/cli/commands/run/detail/RunnableExecutableResolver.hpp>
#include <vix/cli/project/ProjectDiscovery.hpp>
#include <vix/cli/project/NativeProjectCommandDispatch.hpp>
#include <vix/cli/project/TargetSelection.hpp>
#include <vix/cli/project/cmake/CMakeProjectAdapter.hpp>
#include <vix/cli/project/native/NativeDevelopmentRuntime.hpp>
#include <vix/cli/build/BuildContext.hpp>
#include <vix/engine/ExecutionPlanning.hpp>
#include <vix/engine/Watch.hpp>
#include <vix/engine/SanitizerMode.hpp>
#include <vix/cli/Style.hpp>
#include <vix/env/Legacy.hpp>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace vix::cli::style;
namespace fs = std::filesystem;

namespace
{
  using vix::commands::RunCommand::detail::LiveRunResult;
  using vix::commands::RunCommand::detail::Options;

  int run_script_mode(Options &opt);
  int run_executable_direct(
      const fs::path &exePath,
      const Options &opt,
      const std::string &failureContext,
      int timeoutSec,
      bool instrumentedBinary = false,
      std::function<bool()> cancelForWatch = {});
  bool is_executable_file(const fs::path &p);

  enum class RunTargetKind
  {
    Binary,
    Script,
    Project,
    Container,
    Unknown
  };

  struct RunTarget
  {
    RunTargetKind kind{RunTargetKind::Unknown};
    fs::path path{};
  };

  std::string trim_copy_local(std::string s)
  {
    auto is_ws = [](unsigned char c)
    { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };

    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back())))
      s.pop_back();

    std::size_t i = 0;
    while (i < s.size() && is_ws(static_cast<unsigned char>(s[i])))
      ++i;

    s.erase(0, i);
    return s;
  }

  std::string to_dep_folder_name(const std::string &pkg)
  {
    std::string s = pkg;
    const auto at = s.find('@');
    if (at != std::string::npos)
      s = s.substr(0, at);

    const auto slash = s.find('/');
    if (slash == std::string::npos)
      return s;

    return s.substr(0, slash) + "." + s.substr(slash + 1);
  }

  std::vector<std::string> parse_manifest_dep_packages_v1(const fs::path &manifestFile)
  {
    std::ifstream ifs(manifestFile);
    std::vector<std::string> out;
    if (!ifs)
      return out;

    bool inDeps = false;
    std::string line;

    while (std::getline(ifs, line))
    {
      line = trim_copy_local(line);
      if (line.empty() || line[0] == '#')
        continue;

      if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
      {
        const std::string sec = line.substr(1, line.size() - 2);
        inDeps = (sec == "deps");
        continue;
      }

      if (!inDeps)
        continue;

      if (line.rfind("packages", 0) != 0)
        continue;

      const auto eq = line.find('=');
      if (eq == std::string::npos)
        continue;

      std::string rhs = trim_copy_local(line.substr(eq + 1));
      const auto lb = rhs.find('[');
      const auto rb = rhs.rfind(']');
      if (lb == std::string::npos || rb == std::string::npos || rb <= lb)
        continue;

      std::string arr = rhs.substr(lb + 1, rb - lb - 1);

      for (std::size_t i = 0; i < arr.size(); ++i)
      {
        if (arr[i] != '"')
          continue;

        const std::size_t j = arr.find('"', i + 1);
        if (j == std::string::npos)
          break;

        out.push_back(arr.substr(i + 1, j - i - 1));
        i = j;
      }
    }

    return out;
  }

  static int run_normalized_cmake_project(
      const fs::path &projectDir,
      const Options &opt,
      const std::string &requestedName)
  {
    const auto location = vix::cli::project::discover_project(projectDir);
    if (!location.success() ||
        location.location->system != vix::cli::project::ProjectSystem::cmake)
      return -1;

    std::vector<std::string> buildArgs{
        "--dir", location.location->root.string(),
        "--preset", opt.preset,
        "--require-project-metadata",
        "--quiet"};
    for (const auto &argument : opt.cmakeArgs)
    {
      buildArgs.push_back("--cmake");
      buildArgs.push_back(argument);
    }
    if (!requestedName.empty())
    {
      buildArgs.push_back("--build-target");
      buildArgs.push_back(requestedName);
    }
    if (opt.jobs > 0)
    {
      buildArgs.push_back("-j");
      buildArgs.push_back(std::to_string(opt.jobs));
    }
    if (opt.clean)
      buildArgs.push_back("--clean");

    fs::path backendDirectory;
    const int buildCode = vix::commands::BuildCommand::run_internal(buildArgs, &backendDirectory);
    if (buildCode != 0)
      return buildCode;

    if (backendDirectory.empty())
    {
      error("External CMake build did not report its Vix-owned backend directory.");
      return 1;
    }

    const auto snapshot = vix::cli::project::cmake::load_cmake_target_snapshot(
        *location.location, {backendDirectory, std::nullopt});
    if (!snapshot.success())
    {
      error("Unable to resolve configured run targets (" +
            std::string(vix::cli::project::to_string(*snapshot.error)) + ").");
      return 1;
    }

    const auto selected = requestedName.empty()
        ? vix::cli::project::select_default_runnable_target(*snapshot.snapshot)
        : vix::cli::project::select_runnable_target_by_name(*snapshot.snapshot, requestedName);
    if (!selected.success())
    {
      error("Unable to resolve run target" +
            (requestedName.empty() ? std::string{} : " '" + requestedName + "'") +
            " (" + std::string(vix::cli::project::to_string(*selected.error)) + ").");
      return 1;
    }

    const auto artifact = vix::cli::project::cmake::resolve_runnable_artifact(
        *location.location, {backendDirectory, std::nullopt}, selected.target->id);
    if (!artifact.success())
    {
      error("Unable to resolve configured runnable artifact (" +
            std::string(vix::cli::project::to_string(*artifact.error)) + ").");
      return 1;
    }
    if (!is_executable_file(*artifact.artifact))
    {
      error("Configured executable artifact is missing or not executable.");
      hint(artifact.artifact->string());
      return 1;
    }
    return run_executable_direct(
        *artifact.artifact, opt, "Execution failed",
        vix::commands::RunCommand::detail::effective_timeout_sec(opt));
  }

  void apply_manifest_auto_deps_includes(Options &opt, const fs::path &manifestFile)
  {
    const fs::path manifestDir = manifestFile.parent_path();
    const fs::path depsRoot = manifestDir / ".vix" / "deps";

    if (!fs::exists(depsRoot))
      return;

    const auto pkgs = parse_manifest_dep_packages_v1(manifestFile);
    if (pkgs.empty())
      return;

    auto already_has_I = [&](const std::string &inc) -> bool
    {
      const std::string flag = "-I" + inc;
      for (const auto &f : opt.scriptFlags)
      {
        if (f == flag)
          return true;
        if (f.rfind("-I", 0) == 0 && f.substr(2) == inc)
          return true;
      }
      return false;
    };

    for (const auto &p : pkgs)
    {
      const std::string folder = to_dep_folder_name(p);
      const fs::path inc = depsRoot / folder / "include";

      std::error_code ec;
      if (!fs::exists(inc, ec) || ec)
        continue;

      const std::string incStr = inc.string();
      if (!already_has_I(incStr))
        opt.scriptFlags.push_back("-I" + incStr);
    }
  }

  struct RunProgress
  {
    using Clock = std::chrono::steady_clock;

    bool enabled = false;
    int total = 0;
    int current = 0;
    std::string currentLabel;
    Clock::time_point phaseStart{};

    RunProgress(int totalSteps, bool enableUi)
        : enabled(enableUi), total(totalSteps)
    {
    }

    void phase_start(const std::string &label)
    {
      ++current;
      currentLabel = label;
      phaseStart = Clock::now();

      if (!enabled)
        return;

      std::cout << std::endl;
      info("┏ [" + std::to_string(current) + "/" +
           std::to_string(total) + "] " + label);
    }

    void phase_done(const std::string &label, const std::string &extra = {})
    {
      const auto end = Clock::now();
      const auto ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - phaseStart).count();

      if (!enabled)
        return;

      std::string msg =
          "┗ [" + std::to_string(current) + "/" +
          std::to_string(total) + "] " + label;

      if (!extra.empty())
        msg += " - " + extra;

      std::ostringstream oss;
      oss << std::fixed << std::setprecision(2)
          << (static_cast<double>(ms) / 1000.0);

      msg += " (" + oss.str() + "s)";
      success(msg);
    }
  };

  void enable_line_buffered_stdout_for_apps()
  {
#ifndef _WIN32
    ::setenv("VIX_STDOUT_MODE", "line", 1);
#endif
  }

  bool is_executable_file(const fs::path &p)
  {
    std::error_code ec{};
    if (!fs::is_regular_file(p, ec) || ec)
      return false;

#ifdef _WIN32
    return p.extension() == ".exe";
#else
    auto perms = fs::status(p, ec).permissions();
    if (ec)
      return false;

    using pr = fs::perms;
    return (perms & pr::owner_exec) != pr::none ||
           (perms & pr::group_exec) != pr::none ||
           (perms & pr::others_exec) != pr::none;
#endif
  }

  void ensure_mode_env_for_run(const Options &opt)
  {
    const char *cur = vix::env::legacy::getenv("VIX_MODE");
    if (cur && *cur)
      return;

#ifdef _WIN32
    _putenv_s("VIX_MODE", opt.watch ? "dev" : "run");
#else
    ::setenv("VIX_MODE", opt.watch ? "dev" : "run", 1);
#endif
  }

  void apply_docs_env(const Options &opt)
  {
    const bool enabled = opt.docs.has_value() && *opt.docs;

#ifdef _WIN32
    _putenv_s("VIX_DOCS", enabled ? "1" : "0");
#else
    ::setenv("VIX_DOCS", enabled ? "1" : "0", 1);
#endif
  }

  void apply_common_run_environment(Options &opt)
  {
    ensure_mode_env_for_run(opt);
    enable_line_buffered_stdout_for_apps();

    vix::commands::RunCommand::detail::apply_log_env(opt);
    vix::cli::manifest::apply_env_pairs(opt.runEnv);
    apply_docs_env(opt);

#ifndef _WIN32
    ::setenv("VIX_CLI_CLEAR", opt.clearMode.c_str(), 1);
#else
    _putenv_s("VIX_CLI_CLEAR", opt.clearMode.c_str());
#endif
  }

  bool resolve_runtime_cwd(Options &opt, const fs::path &base)
  {
    if (opt.cwd.empty())
      return true;
    fs::path resolved{opt.cwd};
    if (resolved.is_relative())
      resolved = base / resolved;
    resolved = fs::absolute(resolved).lexically_normal();
    std::error_code ec;
    if (!fs::exists(resolved, ec) || ec || !fs::is_directory(resolved, ec) || ec)
    {
      error("Runtime working directory does not exist or is not a directory: " + resolved.string());
      return false;
    }
    opt.cwd = resolved.string();
    return true;
  }

  int run_script_mode(Options &opt)
  {
    if (opt.singleCpp && opt.watch)
      return vix::commands::RunCommand::detail::run_single_cpp_watch(opt);

    if (opt.singleCpp)
    {
      int rc = vix::commands::RunCommand::detail::run_single_cpp(opt);

      if (rc == 130)
      {
        hint("ℹ Program interrupted by user (SIGINT).");
        return 0;
      }

      if (rc < 0)
        return -rc;

      return rc;
    }

    return 1;
  }

  int run_executable_direct(
      const fs::path &exePath,
      const Options &opt,
      const std::string &failureContext,
      int timeoutSec,
      bool instrumentedBinary,
      std::function<bool()> cancelForWatch)
  {
    const bool cancellableWatch = static_cast<bool>(cancelForWatch);
#ifdef _WIN32
    (void)cancelForWatch;
    std::string runCmd = "\"" + exePath.string() + "\"";
    runCmd += vix::commands::RunCommand::detail::join_quoted_args_local(opt.runArgs);
    runCmd = vix::commands::RunCommand::detail::wrap_with_cwd_if_needed(opt, runCmd);

    int raw = std::system(runCmd.c_str());
    int runExit = vix::commands::RunCommand::detail::normalize_exit_code(raw);

    if (runExit == 130 || (cancellableWatch && runExit == 143))
    {
      hint("ℹ Program interrupted by user (SIGINT).");
      return cancellableWatch ? 130 : 0;
    }

    if (runExit != 0)
    {
      vix::commands::RunCommand::detail::handle_runtime_exit_code(
          runExit,
          failureContext,
          false);
      return runExit;
    }

    return 0;
#else
    std::string runCmd = vix::commands::RunCommand::detail::quote(exePath.string());
    runCmd += vix::commands::RunCommand::detail::join_quoted_args_local(opt.runArgs);
    runCmd = vix::commands::RunCommand::detail::wrap_with_cwd_if_needed(opt, runCmd);

    namespace replay = vix::commands::replay;

    replay::ReplayRecorder recorder;
    replay::ReplayRecorderConfig replayConfig{};

    replayConfig.base_dir = fs::current_path();
    replayConfig.cwd = fs::current_path();
    replayConfig.project_dir = fs::current_path();
    replayConfig.target_path = exePath;
    replayConfig.mode = opt.watch ? replay::ReplayMode::Dev : replay::ReplayMode::Run;
    replayConfig.target_kind = replay::ReplayTargetKind::Project;
    replayConfig.command = opt.watch ? "vix dev" : "vix run";
    replayConfig.resolved_command = runCmd;
    replayConfig.app_args = opt.runArgs;
    replayConfig.watch = opt.watch;
    replayConfig.replayable = true;

    std::string replayErr;
    const bool replayEnabled =
        opt.replay && recorder.begin(replayConfig, replayErr);

    replay::ReplayCapture replayCapture;
    if (replayEnabled)
      replayCapture.attach(&recorder);

    const bool useSanRuntime =
        instrumentedBinary ||
        vix::commands::RunCommand::detail::want_any_sanitizer(
            opt.diagnosticMode);

    const LiveRunResult rr =
        vix::commands::RunCommand::detail::run_cmd_live_filtered_capture(
            runCmd,
            "",
            true,
            timeoutSec,
            useSanRuntime,
            false,
            replayEnabled ? &replayCapture : nullptr,
            {},
            std::move(cancelForWatch));

    if (replayEnabled)
    {
      replay::ReplayProcessResult process =
          replay::make_replay_process_result(
              rr.exitCode,
              rr.rawStatus,
              rr.terminatedBySignal,
              rr.termSignal);

      replay::ReplayCapturedResult captured =
          replay::make_replay_captured_result(
              replayCapture.output(),
              process);

      replay::ReplayRecorderFinish finish =
          replay::make_replay_finish_from_capture(captured);

      std::string finishErr;
      (void)recorder.finish(finish, finishErr);
    }

    int runExit = rr.exitCode;

    if (rr.cancelledByWatch && runExit != 130)
      return 0;

    if (runExit == 130 || (cancellableWatch && runExit == 143))
    {
      hint("ℹ Program interrupted by user (SIGINT).");
      return cancellableWatch ? 130 : 0;
    }

    if (runExit != 0)
    {
      std::string log = rr.stderrText;
      if (!rr.stdoutText.empty())
        log += rr.stdoutText;

      bool handled = false;
      const vix::cli::errors::RuntimeCrashEvidence evidence{
          rr.terminatedBySignal,
          rr.termSignal};

      if (vix::cli::errors::RawLogDetectors::hasAuthoritativeRuntimeEvidence(
              log,
              evidence))
      {
        const fs::path diagnosticPath =
            opt.singleCpp
                ? opt.cppFile
                : fs::path{};

        handled = vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
            log,
            diagnosticPath,
            failureContext,
            evidence);

        if (!handled &&
            vix::cli::errors::RawLogDetectors::handleKnownRunFailure(log, diagnosticPath))
        {
          handled = true;
        }

        if (!handled && !rr.printed_live)
          std::cout << log << "\n";
      }

      if (!handled)
      {
        if (cancellableWatch && !rr.printed_live && !log.empty())
          std::cerr << log;
        error(failureContext + " (exit code " + std::to_string(runExit) + ").");
      }

      return runExit;
    }

    vix::commands::RunCommand::detail::emit_captured_program_output(rr);

    return 0;
#endif
  }

  static RunTarget resolve_target(const Options &opt)
  {
    RunTarget t;

    if (!opt.appName.empty())
    {
      const std::string &name = opt.appName;

      // =========================
      // Container runtimes
      // =========================
      if (name.rfind("docker://", 0) == 0 ||
          name.rfind("container://", 0) == 0)
      {
        t.kind = RunTargetKind::Container;
        t.path = name;
        return t;
      }

      // =========================
      // SSH
      // =========================
      if (name.rfind("ssh://", 0) == 0)
      {
        t.kind = RunTargetKind::Container;
        t.path = name;
        return t;
      }

      // =========================
      // HTTP
      // =========================
      if (name.rfind("http://", 0) == 0 ||
          name.rfind("https://", 0) == 0)
      {
        t.kind = RunTargetKind::Container;
        t.path = name;
        return t;
      }
    }

    if (!opt.appName.empty())
    {
      fs::path p = opt.appName;

      std::error_code ec;

      if (fs::exists(p, ec) && !ec)
      {
        const fs::path resolvedPath =
            fs::absolute(p, ec).lexically_normal();

        if (ec)
          return t;

        if (fs::is_regular_file(resolvedPath, ec) && !ec)
        {
          const std::string ext = resolvedPath.extension().string();

          if (ext == ".cpp" || ext == ".cc" || ext == ".cxx")
          {
            t.kind = RunTargetKind::Script;
            t.path = resolvedPath;
            return t;
          }

          if (is_executable_file(resolvedPath))
          {
            t.kind = RunTargetKind::Binary;
            t.path = resolvedPath;
            return t;
          }
        }

        if (fs::is_directory(resolvedPath, ec) && !ec)
        {
          t.kind = RunTargetKind::Project;
          t.path = resolvedPath;
          return t;
        }
      }
    }

    return t;
  }

  static int run_container_target(const std::string &raw, const Options &opt)
  {
#ifndef _WIN32

    auto join_args = [&](const std::vector<std::string> &args)
    {
      std::string out;
      for (const auto &a : args)
      {
        out += " ";
        out += vix::commands::RunCommand::detail::quote(a);
      }
      return out;
    };

    // =========================
    // docker:// / container://
    // =========================
    if (raw.rfind("docker://", 0) == 0 ||
        raw.rfind("container://", 0) == 0)
    {
      std::string image = raw.substr(raw.find("://") + 3);

      const auto space = image.find(' ');
      if (space != std::string::npos)
        image = image.substr(0, space);

      if (image.empty())
      {
        error("Invalid container image.");
        return 1;
      }

      std::string cmd =
          "docker run -it --rm " +
          join_args(opt.runArgs) + " " +
          vix::commands::RunCommand::detail::quote(image);

      return std::system(cmd.c_str());
    }

    // =========================
    // ssh://
    // =========================
    if (raw.rfind("ssh://", 0) == 0)
    {
      std::string target = raw.substr(6);

      std::string cmd =
          "ssh " + vix::commands::RunCommand::detail::quote(target) +
          join_args(opt.runArgs);

      return std::system(cmd.c_str());
    }

    // =========================
    // http://
    // =========================
    if (raw.rfind("http://", 0) == 0 ||
        raw.rfind("https://", 0) == 0)
    {
      std::string cmd =
          "curl -L " + vix::commands::RunCommand::detail::quote(raw) +
          join_args(opt.runArgs);

      return std::system(cmd.c_str());
    }

    error("Unknown runtime target: " + raw);
    return 1;

#else
    error("Container runtime not supported on Windows yet.");
    return 1;
#endif
  }
} // namespace

namespace vix::commands::RunCommand
{
  using namespace detail;

  namespace
  {
#ifndef _WIN32
    static volatile std::sig_atomic_t g_native_dev_stop_requested = 0;

    static void on_native_dev_signal(int)
    {
      g_native_dev_stop_requested = 1;
    }
#endif

    static bool native_dev_relevant_path(const fs::path &path)
    {
      const auto name = path.filename().string();
      const auto ext = path.extension().string();
      return name == "vix.toml" || name == "vix.lock" ||
             ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" ||
             ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx" ||
             ext == ".ipp" || ext == ".inl";
    }

    static bool native_dev_batch_is_relevant(const vix::engine::watch::Batch &batch)
    {
      if (batch.overflowed)
        return true;
      for (const auto &event : batch.events)
      {
        if (native_dev_relevant_path(event.path) ||
            (!event.oldPath.empty() && native_dev_relevant_path(event.oldPath)))
          return true;
      }
      return false;
    }

    static int run_native_project_dev(const Options &opt, const fs::path &projectRoot,
                                      const std::optional<std::string> &target)
    {
#ifdef _WIN32
      (void)opt;
      (void)projectRoot;
      (void)target;
      error("native vix dev process watching is not supported on this platform yet.");
      return 2;
#else
      namespace watch = vix::engine::watch;
      watch::Options watchOptions;
      watchOptions.root = projectRoot;
      watchOptions.recursive = true;
      watchOptions.debounce = std::chrono::milliseconds(60);
      watchOptions.maxBatchWindow = std::chrono::milliseconds(180);
      watchOptions.pollingInterval = std::chrono::milliseconds(100);
      watchOptions.ignoredRoots = {
          projectRoot / ".vix", projectRoot / ".git", projectRoot / ".hg",
          projectRoot / ".svn", projectRoot / "node_modules", projectRoot / ".cache"};

      watch::FileWatcher watcher(std::move(watchOptions));
      const auto started = watcher.start();
      if (!started.ok)
      {
        error("unable to watch native project: " + started.error);
        return 1;
      }

      struct SignalRestore
      {
        using Handler = void (*)(int);
        Handler oldInt{SIG_DFL};
        Handler oldTerm{SIG_DFL};
        ~SignalRestore()
        {
          std::signal(SIGINT, oldInt);
          std::signal(SIGTERM, oldTerm);
        }
      } signalRestore;
      g_native_dev_stop_requested = 0;
      signalRestore.oldInt = std::signal(SIGINT, on_native_dev_signal);
      signalRestore.oldTerm = std::signal(SIGTERM, on_native_dev_signal);

      auto waitForChange = [&]()
      {
        while (!g_native_dev_stop_requested)
        {
          const auto batch = watcher.wait_for_batch(std::chrono::milliseconds(100));
          if (batch && native_dev_batch_is_relevant(*batch))
            return true;
        }
        return false;
      };

      for (;;)
      {
        if (g_native_dev_stop_requested)
          break;

        const auto built = vix::cli::project::native::build_native_runnable_project(
            projectRoot, target, opt.preset,
            vix::cli::project::native::AcquisitionMode::online, nullptr,
            opt.diagnosticMode);
        if (g_native_dev_stop_requested)
          break;
        if (!built.success() || !built.artifactPath)
        {
          error("native dev build failed: " +
                (built.diagnostic.empty() ? "runnable artifact is unavailable" : built.diagnostic));
          if (!waitForChange())
            break;
          continue;
        }

        bool restartRequested = false;
        const int runCode = run_executable_direct(
            *built.artifactPath, opt, "native dev program failed", opt.timeoutSec,
            opt.diagnosticMode != vix::engine::SanitizerMode::None,
            [&]()
            {
              const auto batch = watcher.wait_for_batch(std::chrono::milliseconds(100));
              if (batch && native_dev_batch_is_relevant(*batch))
              {
                restartRequested = true;
                return true;
              }
              return false;
            });
        if (g_native_dev_stop_requested || runCode == 130)
          break;
        if (restartRequested)
          continue;
        if (runCode != 0 && !waitForChange())
          break;
        if (runCode == 0 && !waitForChange())
          break;
      }

      watcher.stop();
      return 0;
#endif
    }

    bool normalize_public_run_arguments(
        const std::vector<std::string> &args,
        std::vector<std::string> &normalized)
    {
      bool afterSeparator = false;
      bool targetSelected = false;
      bool verboseSelected = false;
      bool quietSelected = false;
      std::optional<vix::cli::commands::DiagnosticRequest> diagnosticRequest;
      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const auto &arg = args[i];
        if (!afterSeparator && arg == "--")
        {
          afterSeparator = true;
          normalized.push_back("--");
          continue;
        }
        if (afterSeparator)
        {
          normalized.push_back(arg);
          continue;
        }
        std::string diagnosticError;
        if (vix::cli::commands::select_diagnostic_request_option(
                arg, diagnosticRequest, diagnosticError))
        {
          if (!diagnosticError.empty())
          {
            error(diagnosticError);
            return false;
          }
          normalized.push_back(arg);
          continue;
        }
        if (arg == "-h" || arg == "--help" || arg == "--verbose" || arg == "--quiet" || arg == "-q")
        {
          verboseSelected = verboseSelected || arg == "--verbose";
          quietSelected = quietSelected || arg == "--quiet" || arg == "-q";
          normalized.push_back(arg);
          continue;
        }
        if (arg == "--release")
        {
          normalized.push_back("--preset");
          normalized.push_back("release");
          continue;
        }
        if (arg == "-d" || arg == "--dir" || arg == "--cwd" || arg == "--env")
        {
          if (i + 1 >= args.size() || args[i + 1].empty() || args[i + 1].front() == '-')
          {
            error("Missing value for " + arg);
            return false;
          }
          normalized.push_back(arg);
          const auto value = args[++i];
          if (arg == "--env" && (value.find('=') == std::string::npos || value.front() == '='))
          {
            error("--env requires KEY=VALUE with a non-empty key.");
            return false;
          }
          normalized.push_back(value);
          continue;
        }
        if (arg.rfind("--dir=", 0) == 0 || arg.rfind("--cwd=", 0) == 0 || arg.rfind("--env=", 0) == 0)
        {
          const auto equal = arg.find('=');
          if (equal + 1 == arg.size())
          {
            error("Missing value for " + arg.substr(0, equal));
            return false;
          }
          if (arg.rfind("--env=", 0) == 0)
          {
            const auto assignment = arg.substr(6);
            if (assignment.find('=') == std::string::npos || assignment.front() == '=')
            {
              error("--env requires KEY=VALUE with a non-empty key.");
              return false;
            }
          }
          normalized.push_back(arg);
          continue;
        }
        if (arg == "--cmake" || arg == "--compiler-arg" || arg == "--linker-arg")
        {
          if (i + 1 >= args.size() || args[i + 1] == "--")
          {
            error("Missing value for " + arg);
            return false;
          }
          normalized.push_back(arg);
          normalized.push_back(args[++i]);
          continue;
        }
        if (arg.rfind("--cmake=", 0) == 0 || arg.rfind("--compiler-arg=", 0) == 0 ||
            arg.rfind("--linker-arg=", 0) == 0)
        {
          const auto equal = arg.find('=');
          if (equal + 1 == arg.size())
          {
            error("Missing value for " + arg.substr(0, equal));
            return false;
          }
          normalized.push_back(arg);
          continue;
        }
        if (!arg.empty() && arg.front() == '-')
        {
          error("Unknown run option: " + arg);
          hint("Use: vix run --help");
          return false;
        }
        if (targetSelected)
        {
          error("Run accepts one target or source file; pass program arguments after --.");
          return false;
        }
        const auto inputPath = fs::path{arg};
        if (inputPath.extension() == ".cc" || inputPath.extension() == ".cxx")
        {
          error("Single-source run accepts a .cpp file in format 1.");
          return false;
        }
        std::error_code inputError;
        if (inputPath.extension() != ".cpp" && fs::is_regular_file(inputPath, inputError) && !inputError)
        {
          error("vix run does not accept an arbitrary binary path; select a project target or .cpp source.");
          return false;
        }
        if ((inputPath.has_parent_path() || inputPath.extension() == ".exe") && inputPath.extension() != ".cpp")
        {
          error("vix run accepts a project target or .cpp source, not an arbitrary binary path.");
          return false;
        }
        targetSelected = true;
        normalized.push_back(arg);
      }
      if (verboseSelected && quietSelected)
      {
        error("--verbose and --quiet cannot be used together.");
        return false;
      }
      return true;
    }

    int run_impl(const std::vector<std::string> &args, bool watched)
    {
      std::vector<std::string> publicArgs;
      if (!normalize_public_run_arguments(args, publicArgs))
        return 2;
      Options opt = parse(publicArgs);
      if (watched)
        opt.watch = true;
      if (opt.parseFailed)
        return opt.parseExitCode;
    if (opt.manifestMode)
    {
      vix::cli::manifest::Manifest mf{};
      auto err = vix::cli::manifest::load_manifest(opt.manifestFile, mf);
      if (err)
      {
        error("Invalid .vix manifest: " + err->message);
        hint("File: " + opt.manifestFile.string());
        return 1;
      }

      opt = vix::cli::manifest::merge_options(mf, opt);
    }

    if (opt.manifestMode && !opt.singleCpp)
    {
      opt.singleCpp = true;
      opt.cppFile = manifest_entry_cpp(opt.manifestFile);
    }

    if (opt.hasDoubleDash && !opt.doubleDashArgs.empty())
    {
      if (opt.singleCpp)
      {
        opt.scriptFlags.insert(
            opt.scriptFlags.end(),
            opt.doubleDashArgs.begin(),
            opt.doubleDashArgs.end());
      }
      else
      {
        opt.runArgs.insert(
            opt.runArgs.end(),
            opt.doubleDashArgs.begin(),
            opt.doubleDashArgs.end());
      }

      opt.doubleDashArgs.clear();
    }

    if (opt.manifestMode && opt.singleCpp)
      apply_manifest_auto_deps_includes(opt, opt.manifestFile);

    const auto resolveDiagnosticMode = [&](
        vix::cli::commands::DiagnosticProjectKind projectKind)
    {
      return vix::cli::commands::resolve_diagnostic_mode(
          watched ? vix::cli::commands::DiagnosticCommand::Dev
                  : vix::cli::commands::DiagnosticCommand::Run,
          opt.preset == "release",
          projectKind,
          opt.diagnosticRequest,
          opt.projectDiagnosticRequest);
    };

    apply_common_run_environment(opt);

    if (opt.singleCpp)
    {
      if (!opt.cmakeArgs.empty())
      {
        error("--cmake is available only for an external CMake project.");
        return 2;
      }
      if (!resolve_runtime_cwd(opt, fs::current_path()))
        return 2;
      opt.diagnosticMode = resolveDiagnosticMode(
          vix::cli::commands::DiagnosticProjectKind::StandaloneCpp);
      return run_script_mode(opt);
    }

    // Resolve explicit non-project inputs before considering the containing
    // CMake project.  A URI, executable, script, or container target has its
    // own public execution route and is not a CMake target name.
    const RunTarget directTarget = resolve_target(opt);
    switch (directTarget.kind)
    {
    case RunTargetKind::Binary:
      return run_executable_direct(
          directTarget.path,
          opt,
          "Execution failed",
          detail::effective_timeout_sec(opt));

    case RunTargetKind::Script:
      opt.singleCpp = true;
      opt.cppFile = directTarget.path;
      if (!opt.cmakeArgs.empty())
      {
        error("--cmake is available only for an external CMake project.");
        return 2;
      }
      if (!resolve_runtime_cwd(opt, fs::current_path()))
        return 2;
      opt.diagnosticMode = resolveDiagnosticMode(
          vix::cli::commands::DiagnosticProjectKind::StandaloneCpp);
      return run_script_mode(opt);

    case RunTargetKind::Container:
      return run_container_target(directTarget.path.string(), opt);

    case RunTargetKind::Unknown:
    case RunTargetKind::Project:
      break;
    }

    const fs::path cwd = fs::current_path();
    const fs::path normalizedBase = directTarget.kind == RunTargetKind::Project
        ? directTarget.path
        : (!opt.dir.empty() ? fs::absolute(fs::path(opt.dir)) : cwd);
    const auto normalizedProject =
        vix::cli::project::discover_project(normalizedBase);
    if (!normalizedProject.success() &&
        normalizedProject.error == vix::cli::project::ProjectError::ambiguous_project_authority)
    {
      error("Project contains both vix.toml and CMakeLists.txt; project authority is ambiguous.");
      return 1;
    }
    if (normalizedProject.success())
    {
      if (!opt.compilerArgs.empty() || !opt.linkerArgs.empty())
      {
        error("--compiler-arg and --linker-arg are available only for a single .cpp source file.");
        return 2;
      }
      if (!opt.cmakeArgs.empty() &&
          normalizedProject.location->system != vix::cli::project::ProjectSystem::cmake)
      {
        error("--cmake is available only for an external CMake project.");
        return 2;
      }
      if (!resolve_runtime_cwd(opt, normalizedProject.location->root))
        return 2;
      const auto projectKind =
          normalizedProject.location->system == vix::cli::project::ProjectSystem::native_vix
              ? vix::cli::commands::DiagnosticProjectKind::NativeVix
              : vix::cli::commands::DiagnosticProjectKind::ExternalCMake;
      opt.diagnosticMode = resolveDiagnosticMode(projectKind);

      if (normalizedProject.location->system == vix::cli::project::ProjectSystem::cmake &&
          (opt.diagnosticRequest.kind == vix::cli::commands::DiagnosticRequestKind::Explicit ||
           opt.projectDiagnosticRequest.kind == vix::cli::commands::DiagnosticRequestKind::Explicit))
      {
        error("Vix diagnostic modes are not available for arbitrary external CMake projects.");
        hint("Configure sanitizer instrumentation through the project's CMake options.");
        return 2;
      }
      const std::string requestedName =
          directTarget.kind == RunTargetKind::Project ? std::string{} : opt.appName;

      if (normalizedProject.location->system == vix::cli::project::ProjectSystem::native_vix && opt.watch)
      {
        return run_native_project_dev(
            opt, normalizedProject.location->root,
            requestedName.empty() ? std::nullopt : std::optional<std::string>{requestedName});
      }

      const auto nativeRun = vix::cli::project::dispatch_native_run(
        normalizedProject,
        requestedName.empty() ? std::nullopt : std::optional<std::string>{requestedName},
          opt.runArgs, opt.preset,
        [diagnosticMode = opt.diagnosticMode,
         runtimeCwd = opt.cwd.empty() ? fs::path{} : fs::path{opt.cwd}](const vix::cli::project::ProjectLocation &location,
           const std::optional<std::string> &target, const std::vector<std::string> &arguments,
           std::string_view preset)
        {
          return vix::cli::project::native::run_native_project(
              location.root, target, arguments, preset,
              vix::cli::project::native::AcquisitionMode::online, nullptr,
              diagnosticMode, runtimeCwd);
        });
      if (nativeRun)
      {
        const auto &result = *nativeRun;
        bool handled = false;
        if (!result.success())
        {
          std::string runtimeLog = result.output;
          runtimeLog += result.diagnostic;
          handled = opt.diagnosticMode != vix::engine::SanitizerMode::None &&
              vix::cli::errors::RawLogDetectors::hasAuthoritativeRuntimeEvidence(runtimeLog, {}) &&
              vix::cli::errors::RawLogDetectors::handleRuntimeCrash(runtimeLog, {}, "native run", {});
          if (!handled)
            error("native run failed: " + result.diagnostic);
        }
        if (!result.output.empty() && !handled) std::cout << result.output;
        return result.exitCode;
      }

      if (opt.watch)
      {
#ifndef _WIN32
        return run_project_watch(opt, normalizedProject.location->root);
#else
        hint("Project watch mode is not yet implemented on Windows; running once without auto-reload.");
#endif
      }

      if (normalizedProject.location->system ==
          vix::cli::project::ProjectSystem::cmake)
        return run_normalized_cmake_project(
            normalizedProject.location->root, opt, requestedName);

    }

    if (!opt.cmakeArgs.empty())
    {
      error("--cmake is available only for an external CMake project.");
      return 2;
    }
    if (!opt.compilerArgs.empty() || !opt.linkerArgs.empty())
    {
      error("--compiler-arg and --linker-arg are available only for a single .cpp source file.");
      return 2;
    }

    error("No Vix or CMake project was found for this run target.");
    hint("Run from a project directory or pass --dir <path>.");
    return 1;
    }
  }

  int run(const std::vector<std::string> &args)
  {
    return run_impl(args, false);
  }

  int run_watched(const std::vector<std::string> &args)
  {
    return run_impl(args, true);
  }

  int help()
  {
    std::ostream &out = std::cout;
    out << "Usage:\n";
    out << "  vix run [target|source.cpp] [options] [-- program-args...]\n\n";
    out << "Build if needed, then execute a C++ program once.\n\n";
    out << "Arguments:\n";
    out << "  [target]                  Project target to run\n";
    out << "  [source.cpp]              Compile and run one source file\n";
    out << "\n";
    out << "Options:\n";
    out << "  -d, --dir <path>          Project directory\n";
    out << "  --release                 Build with release settings\n";
    out << "  --cwd <path>              Runtime working directory\n";
    out << "  --env <K=V>               Set a runtime environment variable\n";
    out << "\nAdvanced:\n";
    out << "  --cmake <arg>             One configure argument for external CMake projects\n";
    out << "  --compiler-arg <arg>      One compiler argument for a single source file\n";
    out << "  --linker-arg <arg>        One linker argument for a single source file\n";
    out << "\nDiagnostics:\n";
    out << "  Recommended diagnostics are enabled by default in development when Vix controls compilation.\n";
    out << "  --san                     ASan + UBSan\n";
    out << "  --asan                    Detect memory-safety errors\n";
    out << "  --ubsan                   Detect undefined behavior\n";
    out << "  --tsan                    Detect data races\n";
    out << "  --diagnostics=off         Disable automatic diagnostics\n";
    out << "  --verbose                 Show additional output\n";
    out << "  -q, --quiet               Reduce command output\n";
    out << "  -h, --help                Show this help\n\n";
    out << "Examples:\n";
    out << "  vix run\n";
    out << "  vix run app -- --port 8080\n";
    out << "  vix run src/main.cpp\n";

    return 0;
  }
} // namespace vix::commands::RunCommand
