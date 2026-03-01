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
#include <vix/cli/Style.hpp>
#include <vix/cli/commands/run/RunDetail.hpp>
#include <vix/cli/manifest/VixManifest.hpp>
#include <vix/cli/manifest/RunManifestMerge.hpp>
#include <vix/cli/errors/RawLogDetectors.hpp>
#include <vix/utils/Env.hpp>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <cstring>

#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#endif

using namespace vix::cli::style;
namespace fs = std::filesystem;

namespace
{
  static bool should_clear_terminal_now()
  {
    const char *mode = vix::utils::vix_getenv("VIX_CLI_CLEAR");
    if (!mode || !*mode)
      mode = "auto";

    if (std::strcmp(mode, "never") == 0)
      return false;

#ifndef _WIN32
    if (std::strcmp(mode, "auto") == 0)
      return ::isatty(STDOUT_FILENO) != 0;
#endif

    return true;
  }

  static std::string trim_copy_local(std::string s)
  {
    auto is_ws = [](unsigned char c)
    { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!s.empty() && is_ws((unsigned char)s.back()))
      s.pop_back();
    size_t i = 0;
    while (i < s.size() && is_ws((unsigned char)s[i]))
      ++i;
    s.erase(0, i);
    return s;
  }

  static std::string to_dep_folder_name(const std::string &pkg)
  {
    // "gaspardkirira/strings@0.1.9" -> "gaspardkirira.strings"
    // "gaspardkirira/strings" -> "gaspardkirira.strings"
    std::string s = pkg;
    const auto at = s.find('@');
    if (at != std::string::npos)
      s = s.substr(0, at);

    const auto slash = s.find('/');
    if (slash == std::string::npos)
      return s;

    return s.substr(0, slash) + "." + s.substr(slash + 1);
  }

  static std::vector<std::string> parse_manifest_dep_packages_v1(const std::filesystem::path &manifestFile)
  {
    // Minimal parser for:
    // [deps]
    // packages = ["a/b@x", "c/d@y"]
    std::ifstream ifs(manifestFile);
    std::vector<std::string> out;
    if (!ifs)
      return out;

    bool inDeps = false;
    std::string line;

    while (std::getline(ifs, line))
    {
      line = trim_copy_local(line);
      if (line.empty())
        continue;
      if (line[0] == '#')
        continue;

      if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
      {
        const std::string sec = line.substr(1, line.size() - 2);
        inDeps = (sec == "deps");
        continue;
      }

      if (!inDeps)
        continue;

      const std::string key = "packages";
      if (line.rfind(key, 0) != 0)
        continue;

      const auto eq = line.find('=');
      if (eq == std::string::npos)
        continue;

      std::string rhs = trim_copy_local(line.substr(eq + 1));

      // Expect: ["x","y"]
      const auto lb = rhs.find('[');
      const auto rb = rhs.rfind(']');
      if (lb == std::string::npos || rb == std::string::npos || rb <= lb)
        continue;

      std::string arr = rhs.substr(lb + 1, rb - lb - 1);

      // Split by quotes, minimal
      for (size_t i = 0; i < arr.size(); ++i)
      {
        if (arr[i] != '"')
          continue;
        const size_t j = arr.find('"', i + 1);
        if (j == std::string::npos)
          break;
        out.push_back(arr.substr(i + 1, j - i - 1));
        i = j;
      }
    }

    return out;
  }

  static void apply_manifest_auto_deps_includes(
      vix::commands::RunCommand::detail::Options &opt,
      const std::filesystem::path &manifestFile)
  {
    namespace fs = std::filesystem;

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

  static void clear_terminal_if_enabled()
  {
    if (!should_clear_terminal_now())
      return;

    std::cout << "\033[2J\033[H" << std::flush;
  }

  struct RunProgress
  {
    using Clock = std::chrono::steady_clock;

    bool enabled = false;
    int total;
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

    void phase_log(const std::string &msg)
    {
      if (!enabled)
        return;
      step("┃   " + msg);
    }

    void phase_done(const std::string &label, const std::string &extra = {})
    {
      const auto end = Clock::now();
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - phaseStart).count();

      if (!enabled)
        return;

      std::string msg =
          "┗ [" + std::to_string(current) + "/" +
          std::to_string(total) + "] " + label;

      if (!extra.empty())
        msg += " - " + extra;

      const double seconds = static_cast<double>(ms) / 1000.0;

      std::ostringstream oss;
      oss << std::fixed << std::setprecision(2) << seconds;
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

  static bool is_executable_file(const std::filesystem::path &p)
  {
    std::error_code ec{};
    if (!std::filesystem::is_regular_file(p, ec) || ec)
      return false;

#ifdef _WIN32
    return p.extension() == ".exe";
#else
    auto perms = std::filesystem::status(p, ec).permissions();
    if (ec)
      return false;

    using pr = std::filesystem::perms;
    return (perms & pr::owner_exec) != pr::none ||
           (perms & pr::group_exec) != pr::none ||
           (perms & pr::others_exec) != pr::none;
#endif
  }

  static bool looks_like_test_binary(const std::filesystem::path &p)
  {
    const std::string n = p.filename().string();
    return n.find("_test") != std::string::npos ||
           n.find("_tests") != std::string::npos ||
           n.rfind("test_", 0) == 0;
  }

  static std::optional<std::filesystem::path> find_single_test_binary(const std::filesystem::path &buildDir)
  {
    std::error_code ec{};
    if (!std::filesystem::exists(buildDir, ec) || ec)
      return std::nullopt;

    std::vector<std::filesystem::path> candidates;

    for (auto it = std::filesystem::directory_iterator(buildDir, ec);
         !ec && it != std::filesystem::directory_iterator(); ++it)
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

  static std::optional<std::string> try_choose_run_preset(
      const fs::path &projectDir,
      const std::string &configurePreset,
      const std::string &userRunPreset)
  {
    if (!userRunPreset.empty())
      return userRunPreset;

    try
    {
      const std::string p = vix::commands::RunCommand::detail::choose_run_preset(
          projectDir, configurePreset, userRunPreset);
      if (!p.empty())
        return p;
    }
    catch (...)
    {
    }

    return std::nullopt;
  }

  static std::string default_build_preset_for_configure(const std::string &configurePreset)
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

  static bool ui_enabled()
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

} // namespace

namespace vix::commands::RunCommand
{
  using namespace detail;

  static void ensure_mode_env_for_run(const Options &opt)
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

  static void apply_docs_env(const Options &opt)
  {
    if (!opt.docs.has_value())
      return;

#ifdef _WIN32
    _putenv_s("VIX_DOCS", (*opt.docs ? "1" : "0"));
#else
    ::setenv("VIX_DOCS", (*opt.docs ? "1" : "0"), 1);
#endif
  }

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
      opt.cppFile = detail::manifest_entry_cpp(opt.manifestFile);
    }

    if (opt.hasDoubleDash && !opt.doubleDashArgs.empty())
    {
      if (opt.singleCpp)
      {
        opt.scriptFlags.insert(opt.scriptFlags.end(),
                               opt.doubleDashArgs.begin(),
                               opt.doubleDashArgs.end());
      }
      else
      {
        opt.runArgs.insert(opt.runArgs.end(),
                           opt.doubleDashArgs.begin(),
                           opt.doubleDashArgs.end());
      }

      opt.doubleDashArgs.clear();
    }

    if (opt.manifestMode && opt.singleCpp)
    {
      apply_manifest_auto_deps_includes(opt, opt.manifestFile);
    }

    if (!opt.cwd.empty())
      opt.cwd = normalize_cwd_if_needed(opt.cwd);

    ensure_mode_env_for_run(opt);
    enable_line_buffered_stdout_for_apps();

    apply_log_env(opt);
    vix::cli::manifest::apply_env_pairs(opt.runEnv);
    apply_docs_env(opt);

#ifndef _WIN32
    ::setenv("VIX_CLI_CLEAR", opt.clearMode.c_str(), 1);
#else
    _putenv_s("VIX_CLI_CLEAR", opt.clearMode.c_str());
#endif

    // 1) Mode single .cpp (scripts)
    if (opt.singleCpp && opt.watch)
    {
      return detail::run_single_cpp_watch(opt);
    }

    if (opt.singleCpp)
    {
      int rc = detail::run_single_cpp(opt);

      if (rc < 0)
        return -rc;

      return rc;
    }

    // 2) Mode projet (apps)
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

    if (!opt.singleCpp && opt.watch)
    {
#ifndef _WIN32
      return detail::run_project_watch(opt, projectDir);
#else
      hint("Project watch mode is not yet implemented on Windows; running once without auto-reload.");
#endif
    }

    if (has_presets(projectDir))
    {
      const std::string configurePreset =
          choose_configure_preset_smart(projectDir, opt.preset);

      const std::string buildPreset =
          default_build_preset_for_configure(configurePreset);

      const auto runPresetOpt =
          try_choose_run_preset(projectDir, configurePreset, opt.runPreset);

      const fs::path buildDir =
          resolve_build_dir_smart(projectDir, configurePreset);

      const bool alreadyConfigured = has_cmake_cache(buildDir);

      RunProgress progress(/*totalSteps=*/3, showUi);

      // 1) Configure (only if needed)
      {
        progress.phase_start("Configure project (preset: " + configurePreset + ")");

        bool needConfigure = !alreadyConfigured;

        if (!needConfigure)
        {
          progress.phase_done("Configure project", "cache already present");
        }
        else
        {
          std::string cmd = detail::cmake_configure_cmd(projectDir, configurePreset, buildDir);

#ifdef _WIN32
          // cmake_configure_cmd renvoie deja "cd ... && cmake --preset ..."
          // donc on wrap juste avec cmd /C + cd /D
          cmd = "cmd /C \"cd /D " + quote(projectDir.string()) + " && cmake --preset " + quote(configurePreset) + "\"";
#else
          // si tu veux garder le log-level warning, tu peux l’injecter ici:
          cmd = "cd " + quote(projectDir.string()) + " && cmake --log-level=WARNING --preset " + quote(configurePreset);
#endif

          const LiveRunResult cr = run_cmd_live_filtered_capture(
              cmd,
              /*spinnerLabel=*/"",
              /*passthroughRuntime=*/false,
              /*timeoutSec=*/0);

          const int code = cr.exitCode;

          if (code != 0)
          {
            std::string log = cr.stderrText;
            if (!cr.stdoutText.empty())
              log += cr.stdoutText;

            if (!log.empty())
              std::cout << log << "\n";

            error("CMake configure failed with preset '" + configurePreset + "'.");
            hint("Run the same command manually to inspect the error:");
            step("cd " + projectDir.string());
            step("cmake --preset " + configurePreset);
            return code != 0 ? code : 2;
          }

          progress.phase_done("Configure project", "completed");
        }
      }

      // 2) Build & run preset
      {
        progress.phase_start("Build project (preset: " + buildPreset + ")");

        const std::string mode = opt.watch ? "dev" : "run";
        bool started = false;

        // 2.1) Build only
        {
          std::ostringstream oss;

#ifdef _WIN32
          // Windows: keep live output (cmd) — OK
          oss << "cmd /C \"cd /D " << quote(projectDir.string())
              << " && set VIX_STDOUT_MODE=line"
              << " && set VIX_MODE=" << mode
              << " && cmake --build --preset " << quote(buildPreset);

          if (opt.jobs > 0)
            oss << " -- -j " << opt.jobs;

          oss << "\"";

          const std::string buildCmd = oss.str();
          const int buildCode = run_cmd_live_filtered(
              buildCmd,
              "Building (preset \"" + buildPreset + "\")");

          const int buildExit = normalize_exit_code(buildCode);

          if (buildExit == 130)
          {
            hint("Stopped (SIGINT).");
            return 0;
          }

          if (buildExit != 0)
          {
            error("Build failed (preset '" + buildPreset + "') (exit code " +
                  std::to_string(buildExit) + ").");
            hint("Run the same command manually:");
            step("cd " + projectDir.string());
            step("cmake --build --preset " + buildPreset);
            return buildExit;
          }

#else
          // Linux/macOS: SILENT capture to avoid raw ninja spam; then pretty-print errors.
          oss << "cd " << quote(projectDir.string())
              << " && VIX_STDOUT_MODE=line"
              << " VIX_MODE=" << mode
              << " cmake --build --preset " << quote(buildPreset);

          oss << " --";
          if (opt.jobs > 0)
            oss << " -j " << opt.jobs;

          if (configurePreset.find("ninja") != std::string::npos ||
              buildPreset.find("ninja") != std::string::npos)
          {
            oss << " --quiet";
          }

          const std::string buildCmd = oss.str();

          clear_terminal_if_enabled();

          int rawCode = 0;
          std::string log = run_and_capture_with_code(buildCmd + " 2>&1", rawCode);

          const int buildExit = normalize_exit_code(rawCode);

          if (buildExit == 130)
          {
            hint("Stopped (SIGINT).");
            return 0;
          }

          if (buildExit != 0)
          {
            if (!log.empty())
            {
              vix::cli::ErrorHandler::printBuildErrors(
                  log,
                  projectDir,
                  "Build failed (preset '" + buildPreset + "')");
            }
            else
            {
              error("Build failed (preset '" + buildPreset + "') (exit code " +
                    std::to_string(buildExit) + ").");
            }

            hint("Run the same command manually:");
            step("cd " + projectDir.string());
            step("cmake --build --preset " + buildPreset);

            return buildExit != 0 ? buildExit : 2;
          }

#endif
        }

        // 2.2) Run executable directly
        {
          progress.phase_done("Build project", "completed");
          progress.phase_start("Run application");

          const std::string exeName = projectDir.filename().string();
          fs::path exePath = buildDir / exeName;
#ifdef _WIN32
          exePath += ".exe";
#endif

          if (!fs::exists(exePath))
          {
            if (auto testExe = find_single_test_binary(buildDir))
            {
              if (showUi)
              {
                info("No main executable found. Detected library project; running test binary:");
                step(testExe->string());
              }

#ifdef _WIN32
              clear_terminal_if_enabled();

              std::string cmd = "\"" + testExe->string() + "\"";
              cmd += join_quoted_args_local(opt.runArgs);
              cmd = wrap_with_cwd_if_needed(opt, cmd);
              int raw = std::system(cmd.c_str());

              int testExit = normalize_exit_code(raw);

              if (testExit == 130)
              {
                hint("Stopped (SIGINT).");
                return 0;
              }

              if (testExit != 0)
              {
                error("Test execution failed (exit code " + std::to_string(testExit) + ").");
                return testExit;
              }
#else
              std::string testCmd = quote(testExe->string());
              testCmd += join_quoted_args_local(opt.runArgs);
              testCmd = wrap_with_cwd_if_needed(opt, testCmd);

              const LiveRunResult tr = run_cmd_live_filtered_capture(
                  testCmd,
                  /*spinnerLabel=*/"",
                  /*passthroughRuntime=*/true,
                  /*timeoutSec=*/effective_timeout_sec(opt));

              const int testExit = tr.exitCode;

              if (testExit == 130)
              {
                hint("Stopped (SIGINT).");
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
                  handled = vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
                      log,
                      testExe->string(),
                      "Test crashed");

                  if (!handled && vix::cli::errors::RawLogDetectors::handleKnownRunFailure(log, testExe->string()))
                    handled = true;

                  if (!handled && !tr.printed_live)
                    std::cout << log << "\n";
                }

                if (!handled)
                  error("Test execution failed (exit code " + std::to_string(testExit) + ").");

                return testExit;
              }
#endif

              progress.phase_done("Run application", "test executed");
              if (showUi)
                success("🏃 Test executed (library project).");

              return 0;
            }

            error("Built executable not found: " + exePath.string());
            hint("This looks like a library project. Use: vix tests");
            hint("Resolved build directory: " + buildDir.string());
            hint("If your binary name differs from the folder name, adjust it or add a manifest field to specify target.");
            return 1;
          }

#ifdef _WIN32
          clear_terminal_if_enabled();

          std::string runCmd = "\"" + exePath.string() + "\"";
          runCmd += join_quoted_args_local(opt.runArgs);
          runCmd = wrap_with_cwd_if_needed(opt, runCmd);

          int runCode = std::system(runCmd.c_str());
          int runExit = normalize_exit_code(runCode);

          if (runExit == 130)
          {
            hint("Stopped (SIGINT).");
            return 0;
          }

          if (runExit != 0)
          {
            handle_runtime_exit_code(runExit, "Execution failed", /*alreadyHandled=*/false);
            return runExit;
          }

          started = true;
#else
          std::string runCmd = quote(exePath.string());
          runCmd += join_quoted_args_local(opt.runArgs);
          runCmd = wrap_with_cwd_if_needed(opt, runCmd);

          const LiveRunResult rr = run_cmd_live_filtered_capture(
              runCmd,
              /*spinnerLabel=*/"",
              /*passthroughRuntime=*/true,
              /*timeoutSec=*/effective_timeout_sec(opt));

          int runExit = rr.exitCode;

          if (runExit == 130)
          {
            hint("Stopped (SIGINT).");
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
              handled = vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
                  log,
                  exePath,
                  "Execution failed");

              if (!handled && vix::cli::errors::RawLogDetectors::handleKnownRunFailure(log, exePath))
                handled = true;

              if (!handled && !rr.printed_live)
                std::cout << log << "\n";
            }

            if (!handled)
              error("Execution failed (exit code " + std::to_string(runExit) + ").");

            return runExit;
          }

          started = true;
#endif
        }

        if (started)
        {
          progress.phase_done("Run application", "completed");
          if (showUi)
            success("🏃 Application started (preset: " + buildPreset + ").");
        }
        else
        {
          progress.phase_done("Run application", "stopped");
        }

        return 0;
      }
      return 0;
    }

    fs::path buildDir = projectDir / "build";

    RunProgress progress(/*totalSteps=*/3, showUi);

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
      oss << "cd " << quote(buildDir.string())
          << " && cmake --log-level=WARNING ..";
#endif
      const std::string cmd = oss.str();

      const LiveRunResult cr = run_cmd_live_filtered_capture(
          cmd,
          /*spinnerLabel=*/"",
          /*passthroughRuntime=*/false,
          /*timeoutSec=*/0);

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
    else
    {
      if (showUi)
      {
        info("CMake cache detected in build/ — skipping configure step (fallback).");
        progress.phase_start("Configure project (fallback)");
        progress.phase_done("Configure project", "cache already present");
      }
    }

    {
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
      const int code = run_cmd_live_filtered(
          cmd,
          "Building project (fallback)");

      if (code != 0)
      {
        if (buildLog.empty())
        {
          int captureCode = 0;
          buildLog = run_and_capture_with_code(cmd, captureCode);
          (void)captureCode;
        }

        if (!buildLog.empty())
        {
          vix::cli::ErrorHandler::printBuildErrors(
              buildLog,
              buildDir,
              "Build failed (fallback build/)");
        }
        else
        {
          error("Build failed (fallback build/, code " + std::to_string(code) + ").");
          hint("Check the build command manually in your terminal.");
        }

        return code != 0 ? code : 5;
      }

      if (!buildLog.empty() && has_real_build_work(buildLog))
        std::cout << buildLog;

      progress.phase_done("Build project", "completed (fallback)");
      std::cout << "\n";
    }

    const std::string exeName = projectDir.filename().string();
    fs::path exePath = buildDir / exeName;
#ifdef _WIN32
    exePath += ".exe";
#endif

    if (exeName == "vix")
    {
      success("Build completed (fallback).");

      if (opt.appName == "example")
      {
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
          hint("Existing examples are built in the build/ directory "
               "(e.g. main, now_server, hello_routes...).");
          return 1;
        }

        info("Running example: " + opt.exampleName);
        std::string cmd =
#ifdef _WIN32
            "\"" + exampleExe.string() + "\"";
#else
            quote(exampleExe.string());
#endif

        cmd += join_quoted_args_local(opt.runArgs);
        cmd = wrap_with_cwd_if_needed(opt, cmd);

        int code = std::system(cmd.c_str());
        code = normalize_exit_code(code);

        if (code != 0)
        {
          handle_runtime_exit_code(
              code,
              "Example returned non-zero exit code",
              /*alreadyHandled=*/false);
        }

        return code;
      }

      hint("Detected the Vix umbrella repository.");
      hint("The CLI binary 'vix' and umbrella examples were built in the build/ directory.");
      hint("To run an example from here, use:");
      step("  vix run example main");
      step("  vix run example now_server");
      step("  vix run example hello_routes");
      return 0;
    }

    // Phase 3: run
    if (fs::exists(exePath))
    {
      progress.phase_start("Run application");

      info("Running executable: " + exePath.string());

      auto join_args = [&](const std::vector<std::string> &a) -> std::string
      {
        std::string s;
        for (const auto &x : a)
        {
          if (x.empty())
            continue;
          s += " ";
          s += quote(x);
        }
        return s;
      };

      std::string cmd =
#ifdef _WIN32
          "\"" + exePath.string() + "\"";
#else
          quote(exePath.string());
#endif
      cmd += join_args(opt.runArgs);

      cmd = wrap_with_cwd_if_needed(opt, cmd);

      const int code = std::system(cmd.c_str());

      if (code != 0)
      {
        handle_runtime_exit_code(
            code,
            "Executable returned non-zero exit code",
            /*alreadyHandled=*/false);
        return code;
      }

      progress.phase_done("Run application", "completed");
    }
    else
    {
      progress.phase_start("Run application");
      progress.phase_done("Run application", "no explicit run target");
      success("Build completed (fallback). No explicit 'run' target found.");
      hint("If you want to run a specific example or binary, "
           "execute it manually from the build/ directory.");
    }

    return 0;
  }

  int help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix run [name|file.cpp|manifest.vix] [options] [-- compiler/linker flags] [--run <args...>]\n\n";

    out << "What it does:\n";
    out << "  Build + run a Vix.cpp app.\n";
    out << "  This command supports 3 inputs:\n";
    out << "    1) project name   (vix run api)\n";
    out << "    2) single script  (vix run main.cpp)\n";
    out << "    3) manifest file  (vix run app.vix)\n\n";

    out << "Modes:\n";
    out << "  Project mode:\n";
    out << "    • Finds a CMake project (auto or via --dir)\n";
    out << "    • Uses CMake presets when available (recommended)\n\n";
    out << "  Script mode (.cpp):\n";
    out << "    • Compiles one .cpp file and runs it\n";
    out << "    • Everything after `--` is compiler/linker flags (NOT runtime args)\n";
    out << "    • Use `--run` to pass runtime args (argv) to the script\n\n";
    out << "  Manifest mode (.vix):\n";
    out << "    • Loads a .vix file then merges CLI options on top\n";
    out << "    • If [app].kind=\"project\", it behaves like project mode\n";
    out << "    • If [app].kind=\"script\", it behaves like script mode\n\n";

    out << "Most common mistakes:\n";
    out << "  1) Passing runtime args after `--` (wrong)\n";
    out << "     vix run main.cpp -- --port 8080\n";
    out << "     # `--port` is treated as a compiler flag.\n\n";
    out << "  2) Correct way: use `--run` for runtime args\n";
    out << "     vix run main.cpp --run --port 8080\n";
    out << "     # Alternative: repeatable --args\n";
    out << "     # vix run main.cpp --args --port --args 8080\n\n";

    out << "Options:\n";
    out << "  -d, --dir <path>              Project directory (default: auto-detect)\n";
    out << "  --preset <name>               Configure preset (default: dev-ninja)\n";
    out << "  --run-preset <name>           Build preset for target 'run'\n";
    out << "  -j, --jobs <n>                Parallel build jobs\n";
    out << "  --clear <auto|always|never>   Clear terminal before runtime output (default: auto)\n";
    out << "  --no-clear                    Alias for --clear=never\n\n";

    out << "Runtime options (argv / env):\n";
    out << "  --cwd <path>                  Run the program with this working directory\n";
    out << "  --env <K=V>                   Add/override one env var (repeatable)\n";
    out << "  --args <value>                Add one runtime argument (repeatable)\n\n";
    out << "  --run <args...>               Runtime args for script mode (everything after is argv)\n";

    out << "Watch / lifecycle:\n";
    out << "  --watch, --reload             Rebuild and restart on file changes\n";
    out << "  --force-server                Treat as long-lived (server-like)\n";
    out << "  --force-script                Treat as short-lived (script-like)\n\n";

    out << "Script mode extras:\n";
    out << "  --auto-deps                   Auto-add -I from ./.vix/deps/*/include\n";
    out << "  --auto-deps=local             Same as --auto-deps\n";
    out << "  --auto-deps=up                Also search deps in parent folders (future/optional)\n";
    out << "  --san                         Enable ASan and UBSan\n";
    out << "  --ubsan                       Enable UBSan only\n\n";

    out << "Documentation:\n";
    out << "  --docs                        Enable auto docs (sets VIX_DOCS=1)\n";
    out << "  --no-docs                     Disable auto docs (sets VIX_DOCS=0)\n";
    out << "  --docs=<0|1|true|false>       Explicitly control docs generation\n\n";

    out << "Logging:\n";
    out << "  --log-level <level>           trace | debug | info | warn | error | critical | off\n";
    out << "  --verbose                     Alias for --log-level=debug\n";
    out << "  -q, --quiet                   Alias for --log-level=warn\n";
    out << "  --log-format <kv|json|json-pretty>\n";
    out << "                               Maps to VIX_LOG_FORMAT (default: kv)\n";
    out << "  --log-color <auto|always|never>\n";
    out << "                               Maps to VIX_COLOR (NO_COLOR disables colors)\n";
    out << "  --no-color                    Alias for --log-color=never\n\n";

    out << "Compiler/linker flags (script mode only):\n";
    out << "  Use `--` to stop parsing Vix options and forward flags to the compiler.\n";
    out << "  Example:\n";
    out << "    vix run main.cpp -- -O2 -DNDEBUG -lssl -lcrypto\n\n";

    out << "Examples:\n";
    out << "  # Project mode (auto-detect)\n";
    out << "  vix run\n";
    out << "  vix run api --args --port --args 8080\n";
    out << "  vix run --dir ./examples/blog\n\n";

    out << "  # Project mode + presets\n";
    out << "  vix run api --preset dev-ninja\n";
    out << "  vix run api --preset dev-ninja --run-preset run-dev-ninja\n\n";

    out << "  # Watch mode\n";
    out << "  vix run --watch api\n";
    out << "  vix run --force-server --watch api\n\n";

    out << "  # Script mode (.cpp)\n";
    out << "  vix run main.cpp --cwd ./data --args --config --args config.json\n";
    out << "  vix run main.cpp --run hello 123 test\n";
    out << "  vix run main.cpp -- -O2 -DNDEBUG --run hello 123\n";
    out << "  vix run main.cpp -- -lssl -lcrypto\n\n";

    out << "  # Manifest mode (.vix)\n";
    out << "  vix run app.vix\n";
    out << "  vix run app.vix --args --port --args 8080\n";
    out << "  vix dev app.vix\n\n";

    out << "  # Umbrella repo examples\n";
    out << "  vix run example main\n";
    out << "  vix run example now_server\n\n";

    out << "Environment:\n";
    out << "  VIX_DOCS        0|1             Enable or disable auto docs\n";
    out << "  VIX_LOG_LEVEL   trace|debug|info|warn|error|critical|off\n";
    out << "  VIX_LOG_FORMAT  kv|json|json-pretty\n";
    out << "  VIX_COLOR       auto|always|never   (NO_COLOR disables colors)\n";
    out << "  VIX_STDOUT_MODE line               Used by CLI for smoother live output\n";
    out << "  VIX_CLI_CLEAR   auto|always|never   Clear terminal before runtime output\n\n";

    return 0;
  }

} // namespace vix::commands::RunCommand
