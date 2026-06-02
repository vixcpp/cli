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
#include <vix/cli/commands/run/RunDetail.hpp>
#include <vix/cli/commands/replay/ReplayCapture.hpp>
#include <vix/cli/commands/replay/ReplayRecorder.hpp>
#include <vix/cli/errors/RawLogDetectors.hpp>
#include <vix/cli/manifest/RunManifestMerge.hpp>
#include <vix/cli/manifest/VixManifest.hpp>
#include <vix/cli/app/AppProjectResolver.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace vix::cli::style;
namespace fs = std::filesystem;
namespace app = vix::cli::app;

namespace
{
  using vix::commands::RunCommand::detail::LiveRunResult;
  using vix::commands::RunCommand::detail::Options;

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

  bool should_clear_terminal_now()
  {
    return false;
  }

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

  static void warn_if_env_file_missing(const fs::path &projectDir)
  {
    std::error_code ec;

    const fs::path envFile = projectDir / ".env";
    if (fs::exists(envFile, ec) && !ec)
      return;

    const fs::path envExample = projectDir / ".env.example";
    if (!fs::exists(envExample, ec) || ec)
      return;

    const char *showEnvHint = std::getenv("VIX_SHOW_ENV_HINT");

    if (!showEnvHint || std::string(showEnvHint) != "1")
      return;

    hint(".env not found.");
    step("cp .env.example .env");
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

  static std::optional<fs::path> resolve_runnable_executable(
      const fs::path &buildDir,
      const std::string &projectName)
  {
    auto executable_name = [](const std::string &name) -> std::string
    {
#ifdef _WIN32
      return name + ".exe";
#else
      return name;
#endif
    };

    auto is_executable_candidate = [](const fs::path &p) -> bool
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
    };

    auto looks_like_test_binary = [](const fs::path &p) -> bool
    {
      const std::string n = p.filename().string();
      return n.find("_test") != std::string::npos ||
             n.find("_tests") != std::string::npos ||
             n.rfind("test_", 0) == 0;
    };

    const std::string exeFileName = executable_name(projectName);

    // 1) Exact common locations
    const std::vector<fs::path> preferred = {
        buildDir / exeFileName,
        buildDir / "bin" / exeFileName,
        buildDir / "src" / exeFileName};

    for (const auto &p : preferred)
    {
      if (is_executable_candidate(p))
        return p;
    }

    // 2) Recursive scan
    std::vector<fs::path> exactNameCandidates;
    std::vector<fs::path> otherCandidates;

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

      if (baseName == projectName)
        exactNameCandidates.push_back(p);
      else
        otherCandidates.push_back(p);
    }

    auto prefer_bin_path = [](const fs::path &a, const fs::path &b) -> bool
    {
      const bool aBin = a.string().find("/bin/") != std::string::npos ||
                        a.string().find("\\bin\\") != std::string::npos;
      const bool bBin = b.string().find("/bin/") != std::string::npos ||
                        b.string().find("\\bin\\") != std::string::npos;

      if (aBin != bBin)
        return aBin;

      return a.string().size() < b.string().size();
    };

    if (!exactNameCandidates.empty())
    {
      std::sort(exactNameCandidates.begin(), exactNameCandidates.end(), prefer_bin_path);
      return exactNameCandidates.front();
    }

    if (otherCandidates.size() == 1)
      return otherCandidates.front();

    return std::nullopt;
  }

  static int build_project_with_vix_build(
      const fs::path &projectDir,
      const Options &opt,
      fs::path &outExecutable)
  {
    namespace detail = vix::commands::RunCommand::detail;

    const std::string projectName = projectDir.filename().string();
    const fs::path buildDir = projectDir / "build-ninja";

    std::ostringstream cmd;

#ifdef _WIN32
    cmd << "cmd /C \"cd /D "
        << detail::quote(projectDir.string())
        << " && vix build --build-target "
        << detail::quote(projectName);
#else
    cmd << "cd "
        << detail::quote(projectDir.string())
        << " && vix build --build-target "
        << detail::quote(projectName);
#endif

    if (!opt.preset.empty())
      cmd << " --preset " << detail::quote(opt.preset);

    if (opt.jobs > 0)
      cmd << " -j " << opt.jobs;

    if (opt.verbose)
      cmd << " -v";
    else
      cmd << " -q";

    if (opt.clean)
      cmd << " --clean";

    if (opt.withSqlite)
      cmd << " --with-sqlite";

    if (opt.withMySql)
      cmd << " --with-mysql";

#ifdef _WIN32
    cmd << "\"";
#endif

    const int rawCode =
        detail::run_cmd_live_filtered(
            cmd.str(),
            "Building project");

    const int buildExit =
        detail::normalize_exit_code(rawCode);

    if (buildExit == 130)
    {
      hint("Program interrupted by user.");
      return 0;
    }

    if (buildExit != 0)
      return buildExit;

    auto exePath =
        resolve_runnable_executable(
            buildDir,
            projectName);

    if (!exePath)
    {
      error("Built executable not found for project: " + projectName);
      hint("Resolved build directory: " + buildDir.string());
      hint("If your executable has another name, use the project target name as the folder name for now.");
      return 1;
    }

    outExecutable = *exePath;
    return 0;
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

  void clear_terminal_if_enabled()
  {
    if (!should_clear_terminal_now())
      return;

    std::cout << "\033[2J\033[H" << std::flush;
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

  bool looks_like_test_binary(const fs::path &p)
  {
    const std::string n = p.filename().string();
    return n.find("_test") != std::string::npos ||
           n.find("_tests") != std::string::npos ||
           n.rfind("test_", 0) == 0;
  }

  std::optional<fs::path> find_single_test_binary(const fs::path &buildDir)
  {
    std::error_code ec{};
    if (!fs::exists(buildDir, ec) || ec)
      return std::nullopt;

    std::vector<fs::path> candidates;

    for (auto it = fs::directory_iterator(buildDir, ec);
         !ec && it != fs::directory_iterator();
         ++it)
    {
      const auto &p = it->path();
      if (!is_executable_file(p))
        continue;
      if (!looks_like_test_binary(p))
        continue;
      candidates.push_back(p);
    }

    if (candidates.size() == 1)
      return candidates.front();

    return std::nullopt;
  }

  std::optional<std::string> try_choose_run_preset(
      const fs::path &projectDir,
      const std::string &configurePreset,
      const std::string &userRunPreset)
  {
    if (!userRunPreset.empty())
      return userRunPreset;

    try
    {
      const std::string p =
          vix::commands::RunCommand::detail::choose_run_preset(
              projectDir,
              configurePreset,
              userRunPreset);

      if (!p.empty())
        return p;
    }
    catch (...)
    {
    }

    return std::nullopt;
  }

  std::string default_build_preset_for_configure(const std::string &configurePreset)
  {
    if (configurePreset == "release")
      return "build-release";
    if (configurePreset == "dev-ninja-san")
      return "build-ninja-san";
    if (configurePreset == "dev-ninja-ubsan")
      return "build-ninja-ubsan";
    if (configurePreset == "dev-msvc")
      return "build-msvc";
    return "build-ninja";
  }

  bool ui_enabled()
  {
    const char *v = vix::utils::vix_getenv("VIX_RUN_UI");
    if (!v || !*v)
      return false;
    if (std::strcmp(v, "0") == 0)
      return false;
    if (std::strcmp(v, "false") == 0)
      return false;
    if (std::strcmp(v, "no") == 0)
      return false;
    return true;
  }

  void ensure_mode_env_for_run(const Options &opt)
  {
    const char *cur = vix::utils::vix_getenv("VIX_MODE");
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
    if (!opt.cwd.empty())
      opt.cwd = vix::commands::RunCommand::detail::normalize_cwd_if_needed(opt.cwd);

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

  int run_test_binary_if_present(
      const fs::path &buildDir,
      const std::vector<std::string> &runArgs,
      const std::string &cwd,
      bool showUi)
  {
    auto testExe = find_single_test_binary(buildDir);
    if (!testExe)
      return -1;

    if (showUi)
    {
      info("No main executable found. Detected library project; running test binary:");
      step(testExe->string());
    }

#ifdef _WIN32
    std::string cmd = "\"" + testExe->string() + "\"";
    cmd += vix::commands::RunCommand::detail::join_quoted_args_local(runArgs);

    Options fakeOpt;
    fakeOpt.cwd = cwd;
    cmd = vix::commands::RunCommand::detail::wrap_with_cwd_if_needed(fakeOpt, cmd);

    int raw = std::system(cmd.c_str());
    int testExit = vix::commands::RunCommand::detail::normalize_exit_code(raw);

    if (testExit == 130)
    {
      hint("ℹ Program interrupted by user (SIGINT).");
      return 0;
    }

    if (testExit != 0)
    {
      error("Test execution failed (exit code " + std::to_string(testExit) + ").");
      return testExit;
    }

    return 0;
#else
    std::string testCmd = vix::commands::RunCommand::detail::quote(testExe->string());
    testCmd += vix::commands::RunCommand::detail::join_quoted_args_local(runArgs);

    Options fakeOpt;
    fakeOpt.cwd = cwd;
    testCmd = vix::commands::RunCommand::detail::wrap_with_cwd_if_needed(fakeOpt, testCmd);

    const LiveRunResult tr =
        vix::commands::RunCommand::detail::run_cmd_live_filtered_capture(
            testCmd,
            "",
            true,
            0,
            false);

    const int testExit = tr.exitCode;

    if (testExit == 130)
    {
      hint("ℹ Program interrupted by user (SIGINT).");
      return 0;
    }

    if (testExit != 0)
    {
      std::string log = tr.stderrText;
      if (!tr.stdoutText.empty())
        log += tr.stdoutText;

      bool handled = false;

      if (!log.empty())
      {
        const fs::path diagnosticPath = *testExe;

        handled = vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
            log,
            diagnosticPath,
            "Test crashed");

        if (!handled &&
            vix::cli::errors::RawLogDetectors::handleKnownRunFailure(log, diagnosticPath))
        {
          handled = true;
        }

        if (!handled && !tr.printed_live)
          std::cout << log << "\n";
      }

      if (!handled)
        error("Test execution failed (exit code " + std::to_string(testExit) + ").");

      return testExit;
    }

    return 0;
#endif
  }

  int run_executable_direct(
      const fs::path &exePath,
      const Options &opt,
      const std::string &failureContext,
      int timeoutSec)
  {
#ifdef _WIN32
    std::string runCmd = "\"" + exePath.string() + "\"";
    runCmd += vix::commands::RunCommand::detail::join_quoted_args_local(opt.runArgs);
    runCmd = vix::commands::RunCommand::detail::wrap_with_cwd_if_needed(opt, runCmd);

    int raw = std::system(runCmd.c_str());
    int runExit = vix::commands::RunCommand::detail::normalize_exit_code(raw);

    if (runExit == 130)
    {
      hint("ℹ Program interrupted by user (SIGINT).");
      return 0;
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

    const bool useSanRuntime = vix::commands::RunCommand::detail::want_any_sanitizer(
        opt.enableSanitizers,
        opt.enableUbsanOnly,
        opt.enableThreadSanitizer);

    const LiveRunResult rr =
        vix::commands::RunCommand::detail::run_cmd_live_filtered_capture(
            runCmd,
            "",
            true,
            timeoutSec,
            useSanRuntime,
            false,
            replayEnabled ? &replayCapture : nullptr);

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

    if (runExit == 130)
    {
      hint("ℹ Program interrupted by user (SIGINT).");
      return 0;
    }

    if (runExit != 0)
    {
      std::string log = rr.stderrText;
      if (!rr.stdoutText.empty())
        log += rr.stdoutText;

      bool handled = false;

      if (!log.empty())
      {
        const fs::path diagnosticPath = opt.singleCpp ? opt.cppFile : exePath;

        handled = vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
            log,
            diagnosticPath,
            failureContext);

        if (!handled &&
            vix::cli::errors::RawLogDetectors::handleKnownRunFailure(log, diagnosticPath))
        {
          handled = true;
        }

        if (!handled && !rr.printed_live)
          std::cout << log << "\n";
      }

      if (!handled)
        error(failureContext + " (exit code " + std::to_string(runExit) + ").");

      return runExit;
    }

    return 0;
#endif
  }

  int run_vix_umbrella_example(
      const fs::path &buildDir,
      const Options &opt)
  {
    if (opt.appName != "example")
      return -1;

    if (opt.exampleName.empty())
    {
      error("No example name provided.");
      hint("Usage: vix run example <name>");
      hint("For instance: vix run example main");
      return 1;
    }

    fs::path exampleExe = buildDir / opt.exampleName;
#ifdef _WIN32
    exampleExe += ".exe";
#endif

    if (!fs::exists(exampleExe))
    {
      error("Example binary not found: " + exampleExe.string());
      hint("Make sure the example exists and is enabled in CMake.");
      hint("Existing examples are built in the build/ directory (e.g. main, now_server, hello_routes...).");
      return 1;
    }

    info("Running example: " + opt.exampleName);
    return run_executable_direct(
        exampleExe,
        opt,
        "Example returned non-zero exit code",
        vix::commands::RunCommand::detail::effective_timeout_sec(opt));
  }

  int run_resolved_project(
      const app::AppProjectResolveResult &resolved,
      const Options &opt,
      bool showUi)
  {
    using namespace vix::commands::RunCommand::detail;

    const fs::path buildDir = resolved.userProjectDir / "build-ninja";
    RunProgress progress(3, showUi);

    {
      std::error_code ec;
      fs::create_directories(buildDir, ec);

      if (ec)
      {
        error("Unable to create build directory: " + ec.message());
        return 1;
      }
    }

    const bool alreadyConfigured = has_cmake_cache(buildDir);

    progress.phase_start("Configure project");

    if (alreadyConfigured)
    {
      progress.phase_done("Configure project", "cache already present");
    }
    else
    {
      std::ostringstream oss;

#ifdef _WIN32
      oss << "cmd /C \"cmake"
          << " --log-level=WARNING"
          << " -S " << quote(resolved.cmakeSourceDir.string())
          << " -B " << quote(buildDir.string())
          << " -G Ninja"
          << "\"";
#else
      oss << "cmake"
          << " --log-level=WARNING"
          << " -S " << quote(resolved.cmakeSourceDir.string())
          << " -B " << quote(buildDir.string())
          << " -G Ninja";
#endif

      const LiveRunResult cr = run_cmd_live_filtered_capture(
          oss.str(),
          "",
          false,
          0,
          false);

      const int code = cr.exitCode;

      if (code != 0)
      {
        std::string log = cr.stderrText;

        if (!cr.stdoutText.empty())
          log += cr.stdoutText;

        if (!log.empty())
        {
          const bool handled = vix::cli::ErrorHandler::printBuildErrors(
              log,
              resolved.cmakeListsPath,
              "CMake configure failed");

          if (!handled)
            std::cout << log << "\n";
        }

        error("CMake configure failed.");
        hint("Generated source directory: " + resolved.cmakeSourceDir.string());
        hint("Build directory: " + buildDir.string());

        return code != 0 ? code : 2;
      }

      progress.phase_done("Configure project", "completed");
    }

    progress.phase_start("Build project");

    {
      std::ostringstream oss;

#ifdef _WIN32
      oss << "cmd /C \"cmake --build "
          << quote(buildDir.string());

      if (!resolved.targetName.empty())
        oss << " --target " << quote(resolved.targetName);

      oss << " --";

      if (opt.jobs > 0)
        oss << " -j " << opt.jobs;

      oss << "\"";
#else
      oss << "cmake --build "
          << quote(buildDir.string());

      if (!resolved.targetName.empty())
        oss << " --target " << quote(resolved.targetName);

      oss << " --";

      if (opt.jobs > 0)
        oss << " -j " << opt.jobs;

      oss << " --quiet";
#endif

      clear_terminal_if_enabled();

      int rawCode = 0;
      const std::string buildLog =
          run_and_capture_with_code(oss.str() + " 2>&1", rawCode);

      const int buildExit = normalize_exit_code(rawCode);

      if (buildExit == 130)
      {
        hint("ℹ Program interrupted by user (SIGINT).");
        return 0;
      }

      if (buildExit != 0)
      {
        bool handled = false;

        if (!buildLog.empty())
        {
          handled = vix::cli::ErrorHandler::printBuildErrors(
              buildLog,
              resolved.cmakeListsPath,
              "Build failed");
        }

        if (!handled)
        {
          error("Build failed (exit code " + std::to_string(buildExit) + ").");

          if (!buildLog.empty())
            std::cout << buildLog << "\n";
        }

        return buildExit != 0 ? buildExit : 3;
      }
    }

    progress.phase_done("Build project", "completed");
    progress.phase_start("Run application");

    const std::string exeName =
        !resolved.targetName.empty()
            ? resolved.targetName
            : resolved.userProjectDir.filename().string();

    auto exePathOpt = resolve_runnable_executable(buildDir, exeName);

    if (!exePathOpt)
    {
      const int testRc = run_test_binary_if_present(
          buildDir,
          opt.runArgs,
          opt.cwd,
          showUi);

      if (testRc != -1)
      {
        progress.phase_done("Run application", "test executed");

        if (showUi)
          success("🏃 Test executed.");

        return testRc;
      }

      error("Built executable not found for target: " + exeName);
      hint("Resolved build directory: " + buildDir.string());

      if (resolved.generated)
        hint("Generated CMake source: " + resolved.cmakeSourceDir.string());

      return 1;
    }

    const int runRc = run_executable_direct(
        *exePathOpt,
        opt,
        "Execution failed",
        effective_timeout_sec(opt));

    if (runRc != 0)
      return runRc;

    progress.phase_done("Run application", "completed");

    if (showUi)
    {
      if (resolved.generated)
        success("🏃 Application started from vix.app.");
      else
        success("🏃 Application started.");
    }

    return 0;
  }

  int run_project_with_presets(
      const fs::path &projectDir,
      const Options &opt,
      bool showUi)
  {
    using namespace vix::commands::RunCommand::detail;

    (void)showUi;

    fs::path exePath;

    const int buildCode =
        build_project_with_vix_build(
            projectDir,
            opt,
            exePath);

    if (buildCode != 0)
      return buildCode;

    if (opt.checkOnly)
      return 0;

    const int runCode =
        run_executable_direct(
            exePath,
            opt,
            "Execution failed",
            effective_timeout_sec(opt));

    if (runCode == 130)
    {
      hint("Program interrupted by user.");
      return 0;
    }

    return runCode;
  }

  int run_project_fallback(
      const fs::path &projectDir,
      const Options &opt,
      bool showUi)
  {
    using namespace vix::commands::RunCommand::detail;

    fs::path buildDir = projectDir / "build";
    RunProgress progress(3, showUi);

    {
      std::error_code ec;
      fs::create_directories(buildDir, ec);
      if (ec)
      {
        error("Unable to create build directory: " + ec.message());
        return 1;
      }
    }

    if (!has_cmake_cache(buildDir))
    {
      progress.phase_start("Configure project (fallback)");

      std::ostringstream oss;
#ifdef _WIN32
      oss << "cmd /C \"cd /D " << quote(buildDir.string()) << " && cmake ..\"";
#else
      oss << "cd " << quote(buildDir.string()) << " && cmake --log-level=WARNING ..";
#endif

      const LiveRunResult cr = run_cmd_live_filtered_capture(
          oss.str(),
          "",
          false,
          0,
          false);

      const int code = cr.exitCode;
      if (code != 0)
      {
        std::string log = cr.stderrText;
        if (!cr.stdoutText.empty())
          log += cr.stdoutText;

        if (!log.empty())
          std::cout << log << "\n";

        error("CMake configure failed (fallback build/, code " + std::to_string(code) + ").");
        if (showUi)
          hint("Check your CMakeLists.txt or run the command manually.");

        return code != 0 ? code : 4;
      }

      progress.phase_done("Configure project", "completed (fallback)");
      if (showUi)
        std::cout << "\n";
    }
    else if (showUi)
    {
      info("CMake cache detected in build/ - skipping configure step (fallback).");
      progress.phase_start("Configure project (fallback)");
      progress.phase_done("Configure project", "cache already present");
    }

    progress.phase_start("Build project (fallback)");

    std::string cmd;
#ifdef _WIN32
    cmd = "cd /D " + quote(buildDir.string()) + " && cmake --build .";
    if (opt.jobs > 0)
      cmd += " --parallel " + std::to_string(opt.jobs);
#else
    cmd = "cd " + quote(projectDir.string()) +
          " && cmake --build " + quote(buildDir.string());
    if (opt.jobs > 0)
      cmd += " --parallel " + std::to_string(opt.jobs);
#endif

    std::string buildLog;
    clear_terminal_if_enabled();

    const int code = run_cmd_live_filtered(cmd, "Building project (fallback)");
    if (code != 0)
    {
      if (buildLog.empty())
      {
        int captureCode = 0;
        buildLog = run_and_capture_with_code(cmd, captureCode);
        (void)captureCode;
      }

      bool handled = false;

      if (!buildLog.empty())
      {
        handled = vix::cli::ErrorHandler::printBuildErrors(
            buildLog,
            buildDir,
            "Build failed (fallback build/)");
      }
      else
      {
        error("Build failed (fallback build/, code " + std::to_string(code) + ").");
      }

      if (!handled)
        hint("Check the build command manually in your terminal.");

      return code != 0 ? code : 5;
    }

    if (!buildLog.empty() && has_real_build_work(buildLog))
      std::cout << buildLog;

    progress.phase_done("Build project", "completed (fallback)");
    std::cout << "\n";

    const std::string exeName = projectDir.filename().string();
    auto exePathOpt = resolve_runnable_executable(buildDir, exeName);

    if (!exePathOpt)
    {
      error("Built executable not found for project: " + exeName);
      hint("Resolved build directory: " + buildDir.string());
      hint("No runnable application target could be resolved automatically.");
      hint("If your executable uses a custom output path or custom target name, add a manifest field to specify it.");
      return 1;
    }

    fs::path exePath = *exePathOpt;

    if (exeName == "vix")
    {
      success("Build completed (fallback).");

      const int exRc = run_vix_umbrella_example(buildDir, opt);
      if (exRc != -1)
        return exRc;

      hint("Detected the Vix umbrella repository.");
      hint("The CLI binary 'vix' and umbrella examples were built in the build/ directory.");
      hint("To run an example from here, use:");
      step("  vix run example main");
      step("  vix run example now_server");
      step("  vix run example hello_routes");
      return 0;
    }

    if (fs::exists(exePath))
    {
      progress.phase_start("Run application");
      info("Running executable: " + exePath.string());

      const int runRc = run_executable_direct(
          exePath,
          opt,
          "Executable returned non-zero exit code",
          effective_timeout_sec(opt));

      if (runRc != 0)
        return runRc;

      progress.phase_done("Run application", "completed");
      return 0;
    }

    progress.phase_start("Run application");
    progress.phase_done("Run application", "no explicit run target");
    success("Build completed (fallback). No explicit 'run' target found.");
    hint("If you want to run a specific example or binary, execute it manually from the build/ directory.");
    return 0;
  }

  static std::optional<std::filesystem::path> find_local_binary()
  {
    namespace fs = std::filesystem;

    std::vector<fs::path> candidates;

    for (auto &p : fs::directory_iterator(fs::current_path()))
    {
      if (!p.is_regular_file())
        continue;

      if (!is_executable_file(p.path()))
        continue;

      if (looks_like_test_binary(p.path()))
        continue;

      candidates.push_back(p.path());
    }

    if (candidates.empty())
      return std::nullopt;

    const std::string cwdName = fs::current_path().filename().string();

    for (const auto &c : candidates)
    {
      if (c.filename() == cwdName)
        return c;
    }

    std::vector<fs::path> filtered;

    for (const auto &c : candidates)
    {
      const std::string name = c.filename().string();

      if (name.find("test") != std::string::npos)
        continue;

      if (name.find("bench") != std::string::npos)
        continue;

      filtered.push_back(c);
    }

    if (filtered.size() == 1)
      return filtered[0];

    if (!filtered.empty())
      return filtered[0];

    return candidates[0];
  }

  static std::optional<fs::path> read_last_binary()
  {
    const fs::path metaFile = fs::current_path() / ".vix" / "meta.json";

    std::ifstream in(metaFile);
    if (!in)
      return std::nullopt;

    std::string line;
    while (std::getline(in, line))
    {
      const auto pos = line.find("\"last_binary\"");
      if (pos == std::string::npos)
        continue;

      const auto q1 = line.find('"', pos + 13);
      if (q1 == std::string::npos)
        continue;

      const auto q2 = line.find('"', q1 + 1);
      if (q2 == std::string::npos)
        continue;

      fs::path p = line.substr(q1 + 1, q2 - q1 - 1);

      if (p.is_relative())
        p = fs::current_path() / p;

      return p;
    }

    return std::nullopt;
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

      if (fs::exists(p))
      {
        if (fs::is_regular_file(p))
        {
          if (p.extension() == ".cpp")
          {
            t.kind = RunTargetKind::Script;
            t.path = p;
            return t;
          }

          if (is_executable_file(p))
          {
            t.kind = RunTargetKind::Binary;
            t.path = p;
            return t;
          }
        }

        if (fs::is_directory(p))
        {
          t.kind = RunTargetKind::Project;
          t.path = p;
          return t;
        }
      }
    }

    if (auto bin = read_last_binary())
    {
      if (fs::exists(*bin))
      {
        t.kind = RunTargetKind::Binary;
        t.path = *bin;
        return t;
      }
    }

    if (fs::exists("CMakeLists.txt") || fs::exists("vix.app"))
    {
      t.kind = RunTargetKind::Project;
      t.path = fs::current_path();
      return t;
    }

    if (auto bin = find_local_binary())
    {
      t.kind = RunTargetKind::Binary;
      t.path = *bin;
      return t;
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
        out += a;
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
          image;

      return std::system(cmd.c_str());
    }

    // =========================
    // ssh://
    // =========================
    if (raw.rfind("ssh://", 0) == 0)
    {
      std::string target = raw.substr(6);

      std::string cmd =
          "ssh " + target +
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
          "curl -L " + raw +
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

  int run(const std::vector<std::string> &args)
  {
    Options opt = parse(args);
    const bool showUi = ui_enabled();

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

    apply_common_run_environment(opt);

    if (opt.singleCpp)
      return run_script_mode(opt);

    const RunTarget target = resolve_target(opt);

    switch (target.kind)
    {
    case RunTargetKind::Binary:
      return run_executable_direct(
          target.path,
          opt,
          "Execution failed",
          detail::effective_timeout_sec(opt));

    case RunTargetKind::Script:
      opt.singleCpp = true;
      opt.cppFile = target.path;
      return run_script_mode(opt);

    case RunTargetKind::Container:
      return run_container_target(target.path.string(), opt);

    case RunTargetKind::Project:
    {
      const fs::path baseProjectDir = target.path.empty()
                                          ? fs::current_path()
                                          : target.path;

      const app::AppProjectResolveResult resolved =
          app::resolve_app_project(baseProjectDir);

      if (!resolved.success())
      {
        error("Unable to resolve project.");
        hint(resolved.error);
        return 1;
      }

      warn_if_env_file_missing(resolved.userProjectDir);

      if (!opt.devMode && !opt.watch && project_has_vue_frontend(resolved.userProjectDir))
      {
        print_vue_fullstack_banner();
      }

      if (opt.watch)
      {
#ifndef _WIN32
        return run_project_watch(opt, resolved.userProjectDir);
#else
        hint("Project watch mode is not yet implemented on Windows; running once without auto-reload.");
#endif
      }

      if (!resolved.generated && has_presets(resolved.cmakeSourceDir))
        return run_project_with_presets(resolved.cmakeSourceDir, opt, ui_enabled());

      return run_resolved_project(resolved, opt, ui_enabled());
    }

    default:
      break;
    }

    const bool explicitTarget =
        !opt.appName.empty() ||
        opt.singleCpp ||
        opt.manifestMode ||
        !opt.dir.empty();

    if (!explicitTarget && !opt.checkOnly)
    {
      hint("No run target provided.");
      hint("Use `vix run <target>` to run an executable target.");
      hint("Use `vix run --check` to check/build the current project without running it.");
      return 0;
    }

    const fs::path cwd = fs::current_path();
    auto projectDirOpt = choose_project_dir(opt, cwd);
    if (!projectDirOpt)
    {
      error("Unable to determine the project folder.");
      hint("Try: vix run --dir <path> or run the command from a Vix project directory.");
      return 1;
    }

    const fs::path projectDir = *projectDirOpt;

    if (showUi)
    {
      info("Using project directory:");
      step(projectDir.string());
    }

    warn_if_env_file_missing(projectDir);

    if (!opt.singleCpp && opt.watch)
    {
#ifndef _WIN32
      return run_project_watch(opt, projectDir);
#else
      hint("Project watch mode is not yet implemented on Windows; running once without auto-reload.");
#endif
    }

    const app::AppProjectResolveResult resolved =
        app::resolve_app_project(projectDir);

    if (!resolved.success())
    {
      error("Unable to resolve project.");
      hint(resolved.error);
      return 1;
    }

    if (!opt.devMode && !opt.watch && project_has_vue_frontend(resolved.userProjectDir))
    {
      print_vue_fullstack_banner();
    }

    if (!resolved.generated && has_presets(resolved.cmakeSourceDir))
      return run_project_with_presets(resolved.cmakeSourceDir, opt, showUi);

    return run_resolved_project(resolved, opt, showUi);
  }

  int help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix run [target] [options] [-- compiler/linker flags] [--run <args...>]\n\n";

    out << "Description:\n";
    out << "  Build and run a C++ target with Vix.\n";
    out << "  Works with CMake projects, vix.app projects, single C++ files,\n";
    out << "  .vix manifests, binaries, and runtime targets.\n\n";

    out << "Targets:\n";
    out << "  current project             vix run\n";
    out << "  project directory/name      vix run api\n";
    out << "  single C++ file             vix run main.cpp\n";
    out << "  manifest file               vix run app.vix\n";
    out << "  binary                      vix run ./app\n";
    out << "  Docker image                vix run docker://nginx\n";
    out << "  container image             vix run container://nginx\n";
    out << "  SSH target                  vix run ssh://user@host\n";
    out << "  HTTP target                 vix run http://example.com\n\n";

    out << "Project options:\n";
    out << "  -d, --dir <path>            Project directory\n";
    out << "  --dir=<path>                Same as --dir <path>\n";
    out << "  --preset <name>             Configure/build preset, default: dev-ninja\n";
    out << "  --preset=<name>             Same as --preset <name>\n";
    out << "  --run-preset <name>         Run preset name\n";
    out << "  --run-preset=<name>         Same as --run-preset <name>\n";
    out << "  -j, --jobs <n>              Number of parallel build jobs\n";
    out << "  --jobs=<n>                  Same as --jobs <n>\n";
    out << "  --clean                     Clean/reconfigure before running\n";
    out << "  --check                     Build/check the current project without running it\n";
    out << "  --replay                    Record this run under .vix/runs/\n\n";

    out << "Runtime arguments and environment:\n";
    out << "  --cwd <path>                Run the program from this working directory\n";
    out << "  --cwd=<path>                Same as --cwd <path>\n";
    out << "  --env <K=V>                 Add or override one environment variable\n";
    out << "  --env=<K=V>                 Same as --env <K=V>\n";
    out << "  --args <value>              Add one runtime argument, repeatable\n";
    out << "  --args=<value>              Same as --args <value>\n";
    out << "  --run <args...>             Runtime args for script mode\n\n";

    out << "Watch mode:\n";
    out << "  --watch                     Rebuild and restart on file changes\n";
    out << "  --reload                    Alias for --watch\n";
    out << "  --force-server              Treat the program as a long-running server\n";
    out << "  --force-script              Treat the program as a short-lived script\n\n";

    out << "Script mode:\n";
    out << "  --auto-deps                 Auto-add includes from .vix/deps/*/include\n";
    out << "  --auto-deps=local           Same as --auto-deps\n";
    out << "  --auto-deps=up              Search deps in parent folders too\n";
    out << "  --san                       Enable ASan and UBSan\n";
    out << "  --ubsan                     Enable UBSan only\n";
    out << "  --tsan                      Enable ThreadSanitizer only\n";
    out << "  --with-sqlite               Enable SQLite support\n";
    out << "  --with-mysql                Enable MySQL support\n";
    out << "  --local-cache               Use local .vix-scripts cache\n\n";

    out << "Documentation:\n";
    out << "  --docs                      Enable OpenAPI/docs for this run\n";
    out << "  --no-docs                   Disable OpenAPI/docs for this run\n";
    out << "  --docs=<0|1|true|false>     Explicitly control OpenAPI/docs\n\n";

    out << "Output and logging:\n";
    out << "  --clear <auto|always|never> Clear terminal before runtime output\n";
    out << "  --clear=<auto|always|never> Same as --clear <mode>\n";
    out << "  --no-clear                  Alias for --clear=never\n";
    out << "  --log-level <level>         trace, debug, info, warn, error, critical, off\n";
    out << "  --log-level=<level>         Same as --log-level <level>\n";
    out << "  --verbose                   Alias for --log-level=debug\n";
    out << "  -q, --quiet                 Alias for --log-level=warn\n";
    out << "  --log-format <format>       kv, json, json-pretty\n";
    out << "  --log-format=<format>       Same as --log-format <format>\n";
    out << "  --log-color <mode>          auto, always, never\n";
    out << "  --log-color=<mode>          Same as --log-color <mode>\n";
    out << "  --no-color                  Alias for --log-color=never\n";
    out << "  -h, --help                  Show this help\n\n";

    out << "Compiler/linker flags:\n";
    out << "  -- [flags...]               In script mode, pass flags to the compiler\n";
    out << "                              Example: vix run main.cpp -- -O2 -lssl\n\n";

    out << "Important:\n";
    out << "  For script runtime args, use --run or --args.\n";
    out << "  Everything after -- is treated as compiler/linker flags.\n\n";

    out << "Examples:\n";
    out << "  vix run\n";
    out << "  vix run api\n";
    out << "  vix run --dir ./examples/blog\n";
    out << "  vix run --preset release\n";
    out << "  vix run --clean\n";
    out << "  vix run --watch\n";
    out << "  vix run --reload\n";
    out << "  vix run --watch api\n";
    out << "  vix run --force-server --watch api\n";
    out << "  vix run api --args --port --args 8080\n";
    out << "  vix run api --cwd ./runtime\n";
    out << "  vix run api --env PORT=8080\n";
    out << "  vix run api --replay\n";
    out << "  vix run main.cpp\n";
    out << "  vix run main.cpp --run hello 123\n";
    out << "  vix run main.cpp --args hello --args 123\n";
    out << "  vix run main.cpp --with-sqlite\n";
    out << "  vix run main.cpp --with-mysql\n";
    out << "  vix run main.cpp --san\n";
    out << "  vix run main.cpp -- -O2 -DNDEBUG\n";
    out << "  vix run app.vix\n";
    out << "  vix run app.vix --args --port --args 8080\n";
    out << "  vix run ./app\n";
    out << "  vix run docker://nginx -p 8080:80\n";
    out << "  vix run ssh://localhost echo hello\n";
    out << "  vix run http://example.com\n";
    out << "  vix run example main\n";
    out << "  vix run example now_server\n\n";

    out << "Environment variables:\n";
    out << "  VIX_DOCS                    0 or 1\n";
    out << "  VIX_LOG_LEVEL               trace, debug, info, warn, error, critical, off\n";
    out << "  VIX_LOG_FORMAT              kv, json, json-pretty\n";
    out << "  VIX_COLOR                   auto, always, never\n";
    out << "  VIX_STDOUT_MODE             line\n";
    out << "  VIX_CLI_CLEAR               auto, always, never\n";
    out << "  VIX_SHOW_ENV_HINT=1         Show .env hint when .env.example exists\n\n";

    return 0;
  }
} // namespace vix::commands::RunCommand
