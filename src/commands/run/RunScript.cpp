/**
 *
 *  @file RunScript.cpp
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
#include <vix/cli/commands/run/RunDetail.hpp>
#include <vix/cli/commands/helpers/TextHelpers.hpp>
#include <vix/cli/commands/run/RunScriptHelpers.hpp>
#include <vix/cli/commands/run/detail/DirectScriptRunner.hpp>
#include <vix/cli/commands/run/detail/ScriptProbe.hpp>
#include <vix/cli/commands/run/detail/ScriptCMake.hpp>
#include <vix/cli/commands/replay/ReplayCapture.hpp>
#include <vix/cli/commands/replay/ReplayRecorder.hpp>
#include <vix/cli/errors/RawLogDetectors.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/utils/Env.hpp>
#include <vix/cli/commands/run/dev/DevSession.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace vix::cli::style;

namespace vix::commands::RunCommand::detail
{
  namespace fs = std::filesystem;
  namespace text = vix::cli::commands::helpers;

  namespace
  {
    void print_double_dash_warning_if_needed(const Options &opt);
    bool ensure_script_exists(const fs::path &script);
    void apply_auto_deps_includes_from_deps_folder(Options &opt, const fs::path &startDir);

    struct ScriptProjectState
    {
      fs::path script;
      std::string exeName;
      fs::path scriptsRoot;
      fs::path projectDir;
      fs::path cmakeLists;
      fs::path buildDir;
      fs::path exePath;
      fs::path sigFile;
      fs::path configureLogPath;
      fs::path buildLogPath;

      bool useVixRuntime = false;
      bool needConfigure = true;
      bool skipBuild = false;

      std::string configSignature;
    };

    ScriptProjectState make_state_from_cmake_plan(const CMakeScriptPlan &plan)
    {
      ScriptProjectState state;
      state.script = plan.scriptPath;
      state.exeName = plan.exeName;
      state.scriptsRoot = plan.scriptsRoot;
      state.projectDir = plan.projectDir;
      state.cmakeLists = plan.cmakeListsPath;
      state.buildDir = plan.buildDir;
      state.exePath = plan.exePath;
      state.sigFile = plan.signatureFile;
      state.configureLogPath = plan.configureLogPath;
      state.buildLogPath = plan.buildLogPath;
      state.useVixRuntime = plan.useVixRuntime;
      state.needConfigure = plan.shouldConfigure;
      state.skipBuild = !plan.shouldBuild;
      state.configSignature = plan.configSignature;
      return state;
    }

    bool project_has_registry_lock_dependencies(const fs::path &projectDir)
    {
      const fs::path lockPath = projectDir / "vix.lock";

      if (!fs::exists(lockPath))
        return false;

      std::ifstream ifs(lockPath);
      if (!ifs)
        return false;

      nlohmann::json lock;

      try
      {
        ifs >> lock;
      }
      catch (...)
      {
        return false;
      }

      if (!lock.is_object())
        return false;

      if (!lock.contains("dependencies") || !lock["dependencies"].is_array())
        return false;

      return !lock["dependencies"].empty();
    }

    bool project_registry_deps_installed(const fs::path &projectDir)
    {
      const fs::path vixDir = projectDir / ".vix";
      const fs::path depsDir = vixDir / "deps";
      const fs::path depsCmake = vixDir / "vix_deps.cmake";

      return fs::exists(depsDir) && fs::exists(depsCmake);
    }

    bool ensure_registry_deps_installed_if_needed(const fs::path &projectDir)
    {
      if (!project_has_registry_lock_dependencies(projectDir))
        return true;

      if (project_registry_deps_installed(projectDir))
        return true;

      error("Dependencies not installed");
      hint("run: vix install");
      return false;
    }

    int prepare_script_options_common(Options &opt)
    {
      print_double_dash_warning_if_needed(opt);

      if (!ensure_script_exists(opt.cppFile))
        return 1;

      const fs::path projectDir =
          opt.cppFile.has_parent_path()
              ? fs::absolute(opt.cppFile.parent_path()).lexically_normal()
              : fs::current_path();

      if (!ensure_registry_deps_installed_if_needed(projectDir))
        return 1;

      if (opt.autoDeps != AutoDepsMode::None)
        apply_auto_deps_includes_from_deps_folder(opt, opt.cppFile.parent_path());

      return 0;
    }

    inline bool is_sigint_exit_code(int code) noexcept
    {
      return code == 130;
    }

    std::string trim_copy(std::string s)
    {
      while (!s.empty() &&
             (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
      {
        s.pop_back();
      }

      std::size_t i = 0;
      while (i < s.size() &&
             (s[i] == '\n' || s[i] == '\r' || s[i] == ' ' || s[i] == '\t'))
      {
        ++i;
      }

      s.erase(0, i);
      return s;
    }

    bool log_looks_like_interrupt(const std::string &log)
    {
      const bool isMakeInterrupt =
          (log.find("gmake") != std::string::npos ||
           log.find("make") != std::string::npos) &&
          log.find("Interrupt") != std::string::npos;

      return log.find(" Interrupt") != std::string::npos ||
             isMakeInterrupt ||
             log.find("ninja: interrupted") != std::string::npos ||
             log.find("interrupted by user") != std::string::npos;
    }

    bool log_looks_like_linker_missing_file_due_to_runtime_args(const std::string &log)
    {
      if (log.empty())
        return false;

      const bool hasLd =
          (log.find("/usr/bin/ld:") != std::string::npos) ||
          (log.find(" ld:") != std::string::npos) ||
          (log.find("collect2: error: ld returned") != std::string::npos);

      if (!hasLd)
        return false;

      if (log.find("cannot find ") != std::string::npos &&
          log.find("No such file or directory") != std::string::npos)
      {
        return true;
      }

      if (log.find("cannot find ") != std::string::npos)
        return true;

      return false;
    }

#ifndef _WIN32
    bool log_looks_like_sanitizer_or_ub(const std::string &log)
    {
      return log.find("runtime error:") != std::string::npos ||
             log.find("UndefinedBehaviorSanitizer") != std::string::npos ||
             log.find("AddressSanitizer") != std::string::npos ||
             log.find("LeakSanitizer") != std::string::npos ||
             log.find("ThreadSanitizer") != std::string::npos ||
             log.find("MemorySanitizer") != std::string::npos;
    }

    bool handle_error_tip_block_vix(const std::string &log)
    {
      const auto epos = log.find("error:");
      if (epos == std::string::npos)
        return false;

      auto line_end = [&](std::size_t p) -> std::size_t
      {
        const std::size_t n = log.find('\n', p);
        return (n == std::string::npos) ? log.size() : n;
      };

      auto strip_prefix = [](std::string s, const char *pref) -> std::string
      {
        if (s.rfind(pref, 0) == 0)
          s.erase(0, std::strlen(pref));

        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
          s.erase(0, 1);

        return s;
      };

      const std::size_t eend = line_end(epos);
      std::string eLine = log.substr(epos, eend - epos);

      std::string tipLine;
      const auto tpos = log.find("tip:", eend);
      if (tpos != std::string::npos)
      {
        const std::size_t tend = line_end(tpos);
        tipLine = log.substr(tpos, tend - tpos);
      }

      const std::string msg = strip_prefix(eLine, "error:");
      const std::string tip = tipLine.empty() ? "" : strip_prefix(tipLine, "tip:");

      std::cerr << "  " << RED << "✖" << RESET << " " << msg << "\n";
      if (!tip.empty())
        std::cerr << "  " << GRAY << "➜" << RESET << " " << tip << "\n";

      return true;
    }

    bool dev_verbose_ui(const Options &opt)
    {
      if (opt.verbose)
        return true;

      const char *lvl = vix::utils::vix_getenv("VIX_LOG_LEVEL");
      if (!lvl || !*lvl)
        return false;

      std::string s(lvl);
      for (auto &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      return (s == "debug" || s == "trace");
    }
#endif

#ifndef _WIN32
    bool log_looks_like_runtime_crash(const LiveRunResult &rr, const std::string &log)
    {
      if (rr.terminatedBySignal)
        return true;

      if (log_looks_like_sanitizer_or_ub(log))
        return true;

      return log.find("Segmentation fault") != std::string::npos ||
             log.find("segmentation fault") != std::string::npos ||
             log.find("core dumped") != std::string::npos ||
             log.find("Aborted") != std::string::npos ||
             log.find("terminate called") != std::string::npos ||
             log.find("std::terminate") != std::string::npos ||
             log.find("double free") != std::string::npos ||
             log.find("invalid pointer") != std::string::npos ||
             log.find("stack smashing detected") != std::string::npos;
    }
#endif

    bool has_ccache()
    {
#ifdef _WIN32
      return false;
#else
      int code = std::system("ccache --version >/dev/null 2>&1");
      code = normalize_exit_code(code);
      return code == 0;
#endif
    }

    void print_script_runtime_args_hint()
    {
      hint("It looks like you passed runtime args after `--`.");
      hint("In script mode, `--` forwards compiler/linker flags.");
      hint("Use `--run` for runtime arguments:");
      step("vix run file.cpp --run arg1 arg2 arg3");
      hint("Or use repeatable --args:");
      step("vix run file.cpp --args arg1 --args arg2 --args arg3");
    }

    bool cache_is_ninja_build(const fs::path &buildDir)
    {
      std::error_code ec;
      const fs::path cache = buildDir / "CMakeCache.txt";
      if (!fs::exists(cache, ec) || ec)
        return false;

      std::ifstream ifs(cache);
      if (!ifs)
        return false;

      std::string line;
      while (std::getline(ifs, line))
      {
        if (line.rfind("CMAKE_GENERATOR:INTERNAL=", 0) == 0)
          return line.find("Ninja") != std::string::npos;
      }

      return false;
    }

    std::vector<std::string> extract_script_include_prefixes_for_autodeps(const fs::path &cppPath)
    {
      std::vector<std::string> prefixes;

      std::ifstream ifs(cppPath);
      if (!ifs)
        return prefixes;

      std::string line;
      while (std::getline(ifs, line))
      {
        const auto incPos = line.find("#include");
        if (incPos == std::string::npos)
          continue;

        std::size_t start = line.find('<', incPos);
        char closer = '>';

        if (start == std::string::npos)
        {
          start = line.find('"', incPos);
          closer = '"';
        }

        if (start == std::string::npos)
          continue;

        const std::size_t end = line.find(closer, start + 1);
        if (end == std::string::npos || end <= start + 1)
          continue;

        std::string inc = line.substr(start + 1, end - start - 1);
        if (inc.empty())
          continue;

        const auto slash = inc.find('/');
        const std::string prefix = slash == std::string::npos
                                       ? inc
                                       : inc.substr(0, slash);

        if (!prefix.empty())
          prefixes.push_back(prefix);
      }

      std::sort(prefixes.begin(), prefixes.end());
      prefixes.erase(std::unique(prefixes.begin(), prefixes.end()), prefixes.end());

      return prefixes;
    }

    bool script_may_use_dep_id(const std::vector<std::string> &includePrefixes,
                               const std::string &depId)
    {
      if (includePrefixes.empty() || depId.empty())
        return false;

      std::string normalized = depId;
      normalized.erase(std::remove(normalized.begin(), normalized.end(), '@'), normalized.end());

      const auto slash = normalized.find('/');
      const std::string namespacePart =
          slash == std::string::npos ? normalized : normalized.substr(0, slash);

      const std::string packagePart =
          slash == std::string::npos ? normalized : normalized.substr(slash + 1);

      for (const auto &prefix : includePrefixes)
      {
        if (prefix == normalized)
          return true;

        if (prefix == namespacePart)
          return true;

        if (prefix == packagePart)
          return true;
      }

      return false;
    }

    void apply_auto_deps_includes_from_deps_folder(Options &opt, const fs::path &startDir)
    {
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

      auto home_dir = []() -> std::optional<std::string>
      {
#ifdef _WIN32
        const char *home = vix::utils::vix_getenv("USERPROFILE");
#else
        const char *home = vix::utils::vix_getenv("HOME");
#endif
        if (!home || std::string(home).empty())
          return std::nullopt;
        return std::string(home);
      };

      auto vix_root = [&]() -> fs::path
      {
        if (const auto home = home_dir(); home)
          return fs::path(*home) / ".vix";
        return fs::path(".vix");
      };

      auto global_manifest_path = [&]() -> fs::path
      {
        return vix_root() / "global" / "installed.json";
      };

      auto dep_id_to_dir_local = [](std::string depId) -> std::string
      {
        depId.erase(std::remove(depId.begin(), depId.end(), '@'), depId.end());
        std::replace(depId.begin(), depId.end(), '/', '.');
        return depId;
      };

      std::unordered_set<std::string> localPkgDirs;

      const std::vector<std::string> scriptIncludePrefixes =
          extract_script_include_prefixes_for_autodeps(startDir / opt.cppFile.filename());

      auto scan_one = [&](const fs::path &baseDir)
      {
        const fs::path depsRoot = baseDir / ".vix" / "deps";
        std::error_code ec;

        if (!fs::exists(depsRoot, ec) || ec)
          return;

        for (auto it = fs::directory_iterator(depsRoot, ec);
             !ec && it != fs::directory_iterator(); ++it)
        {
          if (!it->is_directory())
            continue;

          const std::string pkgDir = it->path().filename().string();
          if (!pkgDir.empty())
            localPkgDirs.insert(pkgDir);

          const fs::path inc = it->path() / "include";
          if (!fs::exists(inc, ec) || ec)
            continue;

          const std::string incStr = inc.string();
          if (!already_has_I(incStr))
            opt.scriptFlags.push_back("-I" + incStr);
        }
      };

      auto scan_global = [&]()
      {
        const fs::path manifestPath = global_manifest_path();
        if (!fs::exists(manifestPath))
          return;

        std::ifstream ifs(manifestPath);
        if (!ifs)
          return;

        nlohmann::json root;
        ifs >> root;

        if (!root.is_object() || !root.contains("packages") || !root["packages"].is_array())
          return;

        for (const auto &item : root["packages"])
        {
          if (!item.is_object())
            continue;

          if (!item.contains("id") || !item["id"].is_string())
            continue;

          if (!item.contains("installed_path") || !item["installed_path"].is_string())
            continue;

          const std::string id = item["id"].get<std::string>();

          if (!script_may_use_dep_id(scriptIncludePrefixes, id))
            continue;

          const std::string pkgDir = dep_id_to_dir_local(id);

          if (localPkgDirs.contains(pkgDir))
            continue;

          std::string includeDir = "include";
          if (item.contains("include") && item["include"].is_string())
            includeDir = item["include"].get<std::string>();

          const fs::path installedPath = fs::path(item["installed_path"].get<std::string>());
          const fs::path inc = installedPath / includeDir;

          std::error_code ec;
          if (!fs::exists(inc, ec) || ec)
            continue;

          const std::string incStr = inc.string();
          if (!already_has_I(incStr))
            opt.scriptFlags.push_back("-I" + incStr);
        }
      };

      if (opt.autoDeps == AutoDepsMode::Local)
      {
        scan_one(startDir);
        scan_global();
        return;
      }

      if (opt.autoDeps == AutoDepsMode::Up)
      {
        fs::path cur = startDir;
        for (;;)
        {
          scan_one(cur);

          std::error_code ec;
          fs::path parent = cur.parent_path();
          if (parent.empty() || parent == cur)
            break;

          if (fs::equivalent(parent, cur, ec) && !ec)
            break;

          cur = parent;
        }

        scan_global();
      }
    }

    bool ensure_script_exists(const fs::path &script)
    {
      if (fs::exists(script))
        return true;

      error("C++ file not found: " + script.string());
      return false;
    }

    void print_double_dash_warning_if_needed(const Options &opt)
    {
      if (!opt.warnedVixFlagAfterDoubleDash)
        return;

      hint("Note: '" + opt.warnedArg + "' was passed after `--` so it will be treated as a compiler/linker flag.");
      hint("If you meant a Vix option, move it before `--`.");
      hint("If you meant a runtime arg, use `--run` (or repeatable --args).");
    }

    int configure_script_project(const Options &opt, const ScriptProjectState &state)
    {
      if (!state.needConfigure)
        return 0;

      std::ostringstream oss;
      oss << "cd " << quote(state.projectDir.string())
          << " && cmake -S . -B build-ninja -G Ninja";

      if (has_ccache())
      {
        oss << " -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
            << " -DCMAKE_C_COMPILER_LAUNCHER=ccache";
      }

      if (want_any_sanitizer(
              opt.enableSanitizers,
              opt.enableUbsanOnly,
              opt.enableThreadSanitizer))
      {
        oss << " -DVIX_ENABLE_SANITIZERS=ON"
            << " -DVIX_SANITIZER_MODE="
            << sanitizer_mode_string(
                   opt.enableSanitizers,
                   opt.enableUbsanOnly,
                   opt.enableThreadSanitizer);
      }
      else
      {
        oss << " -DVIX_ENABLE_SANITIZERS=OFF";
      }

      const std::string cmd = oss.str();

      if (vix::utils::vix_getenv("VIX_PROCESS_DEBUG"))
      {
        std::cerr << "[vix:process] script configure cmd="
                  << cmd
                  << "\n";
      }

      const LiveRunResult configureResult = run_cmd_live_filtered_capture(
          cmd,
          "Configuring script...",
          false,
          0,
          false,
          false,
          nullptr);

      const int code = normalize_exit_code(configureResult.exitCode);

      {
        std::ofstream logOut(
            state.configureLogPath,
            std::ios::binary | std::ios::trunc);

        if (logOut)
        {
          logOut << configureResult.stdoutText;

          if (!configureResult.stderrText.empty())
            logOut << configureResult.stderrText;
        }
      }

      if (code == 0)
      {
        (void)text::write_text_file(state.sigFile, state.configSignature);
        return 0;
      }

      std::ifstream ifs(state.configureLogPath);
      std::string logContent;

      if (ifs)
      {
        std::ostringstream ss;
        ss << ifs.rdbuf();
        logContent = ss.str();
      }

      if (is_sigint_exit_code(code) || log_looks_like_interrupt(logContent))
      {
        error("Configure interrupted by user (SIGINT).");
        hint("Nothing is wrong: you stopped the configure step.");
        return code;
      }

      bool handled = false;

      if (!logContent.empty())
      {
        handled = vix::cli::ErrorHandler::printBuildErrors(
            logContent,
            state.script,
            "Script configure failed");
      }

      if (!handled)
      {
        error("Script configure failed.");
        hint("CMake output was captured in:");
        step(state.configureLogPath.string());
      }

      handle_runtime_exit_code(code, "Script configure failed", handled);
      return code;
    }

    bool can_skip_build(const Options &opt, const ScriptProjectState &state)
    {
#ifdef _WIN32
      (void)opt;
      (void)state;
      return false;
#else
      if (state.needConfigure)
        return false;

      if (needs_rebuild_from_depfiles_cached(state.exePath, state.buildDir, state.exeName))
        return false;

      if (!opt.quiet)
        hint("Up to date (skip build).");

      return true;
#endif
    }

    int build_script_project(const Options &opt, const ScriptProjectState &state)
    {
      if (state.skipBuild)
        return 0;

      std::ostringstream oss;
      oss << "cd " << quote(state.projectDir.string())
          << " && cmake --build build-ninja --target " << state.exeName;

#ifdef _WIN32
      if (opt.jobs > 0)
        oss << " -- /m:" << opt.jobs;
#else
      if (opt.jobs > 0)
        oss << " -- -j " << opt.jobs;
#endif

      oss << " >" << quote(state.buildLogPath.string()) << " 2>&1";

      const std::string buildCmd = oss.str();
      int code = std::system(buildCmd.c_str());
      code = normalize_exit_code(code);

      if (code == 0)
        return 0;

      std::ifstream ifs(state.buildLogPath);
      std::string logContent;

      if (ifs)
      {
        std::ostringstream logStream;
        logStream << ifs.rdbuf();
        logContent = logStream.str();
      }

      if (is_sigint_exit_code(code) || log_looks_like_interrupt(logContent))
      {
        error("Build interrupted by user (SIGINT).");
        hint("Nothing is wrong: you stopped the build.");
        return code;
      }

      bool handled = false;

      if (!logContent.empty())
      {
        if (log_looks_like_linker_missing_file_due_to_runtime_args(logContent))
          print_script_runtime_args_hint();

        handled = vix::cli::ErrorHandler::printBuildErrors(
            logContent,
            state.script,
            "Script build failed");
      }
      else
      {
        error("Script build failed (no compiler log captured).");
      }

      handle_runtime_exit_code(code, "Script build failed", handled);
      return code;
    }

    int ensure_script_executable_exists(const ScriptProjectState &state)
    {
      if (fs::exists(state.exePath))
        return 0;

      error("Script binary not found: " + state.exePath.string());
      return 1;
    }

#ifndef _WIN32
    int run_script_binary_posix(const Options &opt, const ScriptProjectState &state)
    {
      apply_sanitizer_env_if_needed(
          opt.enableSanitizers,
          opt.enableUbsanOnly,
          opt.enableThreadSanitizer);

      // A script using vix::io (stdin/stdout) needs stdin forwarded just like
      // a plain script. Only true long-lived server apps should suppress passthrough.
      // Use forceServerLike to distinguish: if the user didn't explicitly say
      // --force-server, treat any non-watch script as interactive (passthrough).
      const bool isInteractive = !opt.forceServerLike || !state.useVixRuntime;

      std::string cmdRun = "VIX_STDOUT_MODE=line " + quote(state.exePath.string());
      cmdRun += join_quoted_args_local(opt.runArgs);
      cmdRun = wrap_with_cwd_if_needed(opt, cmdRun);

      namespace replay = vix::commands::replay;

      replay::ReplayRecorder recorder;
      replay::ReplayRecorderConfig replayConfig{};

      replayConfig.base_dir = fs::current_path();
      replayConfig.cwd = fs::current_path();
      replayConfig.project_dir = fs::current_path();
      replayConfig.target_path = state.script;
      replayConfig.mode = opt.watch ? replay::ReplayMode::Dev : replay::ReplayMode::Run;
      replayConfig.target_kind = replay::ReplayTargetKind::SingleCpp;
      replayConfig.command = opt.watch
                                 ? "vix dev " + state.script.string()
                                 : "vix run " + state.script.string();
      replayConfig.resolved_command = cmdRun;
      replayConfig.vix_args = opt.scriptFlags;
      replayConfig.app_args = opt.runArgs;
      replayConfig.watch = opt.watch;
      replayConfig.direct_script = false;
      replayConfig.cmake_fallback = true;
      replayConfig.replayable = true;

      std::string replayErr;
      const bool replayEnabled =
          opt.replay && recorder.begin(replayConfig, replayErr);

      replay::ReplayCapture replayCapture;
      if (replayEnabled)
        replayCapture.attach(&recorder);

      const bool useSanRuntime = want_any_sanitizer(
          opt.enableSanitizers,
          opt.enableUbsanOnly,
          opt.enableThreadSanitizer);

      LiveRunResult rr = run_cmd_live_filtered_capture(
          cmdRun,
          "",
          isInteractive,
          effective_timeout_sec(opt),
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

      int runCode = normalize_exit_code(rr.exitCode);

      std::string out = rr.stdoutText;
      std::string err = rr.stderrText;

      const std::string outT = trim_copy(out);
      const std::string errT = trim_copy(err);

      if (!outT.empty() && outT == errT)
      {
        err.clear();
      }
      else if (!outT.empty() && !errT.empty())
      {
        if (outT.find(errT) != std::string::npos)
          err.clear();
        else if (errT.find(outT) != std::string::npos)
          out.clear();
      }

      std::string runtimeLog;
      runtimeLog.reserve(out.size() + err.size() + 1);

      if (!out.empty())
        runtimeLog += out;

      if (!err.empty())
      {
        if (!runtimeLog.empty() && runtimeLog.back() != '\n')
          runtimeLog.push_back('\n');
        runtimeLog += err;
      }

      const bool interruptedBySigint =
          is_sigint_exit_code(runCode) ||
          (rr.terminatedBySignal && rr.termSignal == SIGINT) ||
          log_looks_like_interrupt(runtimeLog);

      if (interruptedBySigint)
      {
        hint("ℹ Program interrupted by user (SIGINT).");
        return 0;
      }

      const bool looksSanOrUb =
          !runtimeLog.empty() && log_looks_like_sanitizer_or_ub(runtimeLog);

      const bool noOutput =
          trim_copy(rr.stdoutText).empty() &&
          trim_copy(rr.stderrText).empty();

      if (runCode == 0 && !looksSanOrUb && noOutput)
      {
        if (!opt.quiet || ::isatty(STDOUT_FILENO) != 0)
          vix::cli::style::hint("Program exited successfully (code 0) but produced no output.");

        return 0;
      }

      if (runCode != 0 || looksSanOrUb)
      {
        if (runCode == 0 && looksSanOrUb)
          runCode = 1;

        bool handled = false;

        if (!runtimeLog.empty())
        {
          handled = handle_error_tip_block_vix(runtimeLog);

          const bool maybeRuntimeCrash =
              !handled && log_looks_like_runtime_crash(rr, runtimeLog);

          if (maybeRuntimeCrash)
          {
            handled = vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
                runtimeLog,
                state.script,
                "Script execution failed");

            if (!handled &&
                vix::cli::errors::RawLogDetectors::handleKnownRunFailure(runtimeLog, state.script))
            {
              handled = true;
            }
          }

          if (!handled && !rr.printed_live)
            std::cerr << runtimeLog << "\n";
        }

        const bool already = handled || rr.printed_live;
        handle_runtime_exit_code(runCode, "Script execution failed", already);

        if (already && runCode > 0 && runCode != 130)
          return -runCode;

        return runCode;
      }

      return 0;
    }
#else
    int run_script_binary_windows(const Options &opt, const ScriptProjectState &state)
    {
      std::string cmdRun =
          "cmd /C \"set VIX_STDOUT_MODE=line && \"" + state.exePath.string() + "\"";
      cmdRun += join_quoted_args_local(opt.runArgs);
      cmdRun += "\"";

      cmdRun = wrap_with_cwd_if_needed(opt, cmdRun);

      const bool useSanRuntime = want_any_sanitizer(
          opt.enableSanitizers,
          opt.enableUbsanOnly,
          opt.enableThreadSanitizer);

      const LiveRunResult rr = run_cmd_live_filtered_capture(
          cmdRun,
          "",
          true,
          effective_timeout_sec(opt),
          useSanRuntime);

      int runCode = normalize_exit_code(rr.exitCode);

      if (runCode != 0)
      {
        std::string log = rr.stderrText;
        if (!rr.stdoutText.empty())
          log += rr.stdoutText;

        bool handled = false;

        if (!log.empty())
        {
          handled = vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
              log,
              state.script,
              "Script execution failed");

          if (!handled &&
              vix::cli::errors::RawLogDetectors::handleKnownRunFailure(log, state.script))
          {
            handled = true;
          }

          if (!handled && !rr.printed_live)
            std::cerr << log << "\n";
        }

        handle_runtime_exit_code(runCode, "Script execution failed", handled);
        return runCode;
      }

      return 0;
    }
#endif

    int materialize_cmake_script_project(const Options &opt, const ScriptProjectState &state)
    {
      std::error_code ec;
      fs::create_directories(state.projectDir, ec);
      if (ec)
      {
        error("Failed to create script project directory.");
        return 1;
      }

      const std::string cmakeText = make_script_cmakelists(
          state.exeName,
          state.script,
          state.useVixRuntime,
          opt.scriptFlags,
          opt.withSqlite,
          opt.withMySql);

      {
        std::ofstream out(state.cmakeLists, std::ios::trunc);
        if (!out)
        {
          error("Failed to write generated CMakeLists.txt.");
          return 1;
        }

        out << cmakeText;

        if (!out)
        {
          error("Failed to write generated CMakeLists.txt completely.");
          return 1;
        }
      }

      return 0;
    }

    void compute_need_configure(ScriptProjectState &state)
    {
      state.needConfigure = true;

      std::error_code ec{};
      if (fs::exists(state.buildDir / "CMakeCache.txt", ec) && !ec)
      {
        const std::string oldSig = text::read_text_file_or_empty(state.sigFile);
        if (!oldSig.empty() && oldSig == state.configSignature)
          state.needConfigure = false;
      }

      if (!cache_is_ninja_build(state.buildDir))
      {
        std::error_code rmEc;
        fs::remove_all(state.buildDir, rmEc);
        state.needConfigure = true;
      }
    }

    int configure_and_build_script(Options &o, ScriptProjectState &state)
    {
      const int materializeCode = materialize_cmake_script_project(o, state);
      if (materializeCode != 0)
        return materializeCode;

      compute_need_configure(state);

      if (o.clean)
      {
        std::error_code ec;
        fs::remove_all(state.buildDir, ec);
        fs::remove(state.sigFile, ec);

        const int rematerializeCode = materialize_cmake_script_project(o, state);
        if (rematerializeCode != 0)
          return rematerializeCode;

        state.needConfigure = true;
        state.skipBuild = false;
      }

      const int cfgCode = configure_script_project(o, state);
      if (cfgCode != 0)
        return cfgCode;

      state.skipBuild = can_skip_build(o, state);

      const int buildCode = build_script_project(o, state);
      if (buildCode != 0)
        return buildCode;

      return ensure_script_executable_exists(state);
    }

    int build_script_executable_internal(const Options &opt, fs::path &exePath)
    {
      Options o = opt;

      const int prepCode = prepare_script_options_common(o);
      if (prepCode != 0)
        return prepCode;

      const ScriptProbeResult probe = probe_single_cpp_script(o);

      if (script_can_use_direct_compile(probe))
      {
        const DirectScriptPlan directPlan = make_direct_script_plan(o, probe);

        std::error_code ec;
        fs::create_directories(directPlan.cacheDir, ec);
        if (ec)
        {
          error("Failed to create direct script cache directory.");
          return 1;
        }

#ifndef _WIN32
        apply_sanitizer_env_if_needed(
            o.enableSanitizers,
            o.enableUbsanOnly,
            o.enableThreadSanitizer);
#endif

        const DirectScriptCacheState cache = load_direct_script_cache_state(directPlan);

        if (directPlan.shouldCompile)
        {
          const LiveRunResult build = run_cmd_live_filtered_capture(
              directPlan.compileCmd,
              "Compiling script...",
              false,
              0,
              o.enableSanitizers || o.enableUbsanOnly,
              true);

          if (build.exitCode != 0)
          {
            bool handled = false;

            if (!build.stdoutText.empty() || !build.stderrText.empty())
            {
              const std::string compileLog = build.stdoutText + build.stderrText;
              handled = vix::cli::ErrorHandler::printBuildErrors(
                  compileLog,
                  directPlan.scriptPath,
                  "Script compile failed");
            }

            if (!handled)
              error("Script compile failed.");

            return build.exitCode != 0 ? build.exitCode : 1;
          }

          // Do not rewrite legacy cache metadata here.
          // The direct runner owns the cache metadata format.
        }

        exePath = directPlan.binaryPath;
        return 0;
      }

      const CMakeScriptPlan cmakePlan = make_cmake_script_plan(o, probe);
      ScriptProjectState state = make_state_from_cmake_plan(cmakePlan);

      if (o.clean)
      {
        std::error_code ec;
        fs::remove_all(state.buildDir, ec);
        fs::remove(state.sigFile, ec);
        state.needConfigure = true;
        state.skipBuild = false;
      }

      const int code = configure_and_build_script(o, state);
      if (code != 0)
        return code;

      exePath = state.exePath;
      return 0;
    }

    unsigned long long fnv1a_64(const std::string &input)
    {
      constexpr unsigned long long offset = 14695981039346656037ULL;
      constexpr unsigned long long prime = 1099511628211ULL;

      unsigned long long hash = offset;
      for (char c : input)
      {
        const unsigned char uc = static_cast<unsigned char>(c);
        hash ^= static_cast<unsigned long long>(uc);
        hash *= prime;
      }

      return hash;
    }

    std::string hex_u64(unsigned long long value)
    {
      static constexpr char digits[] = "0123456789abcdef";
      std::string out(16, '0');

      for (int i = 15; i >= 0; --i)
      {
        out[static_cast<std::size_t>(i)] = digits[value & 0xF];
        value >>= 4ULL;
      }

      return out;
    }

  } // namespace

  CMakeScriptPlan make_cmake_script_plan(
      const Options &opt,
      const ScriptProbeResult &probe)
  {
    CMakeScriptPlan plan{};

    plan.scriptPath = fs::absolute(opt.cppFile).lexically_normal();
    plan.exeName = plan.scriptPath.stem().string();
    plan.scriptsRoot = get_scripts_root(opt.localCache);

    const std::string scriptCacheKey = hex_u64(
        fnv1a_64("script-cache:" + plan.scriptPath.string()));

    plan.projectDir = plan.scriptsRoot / scriptCacheKey;
    plan.cmakeListsPath = plan.projectDir / "CMakeLists.txt";
    plan.buildDir = plan.projectDir / "build-ninja";
    plan.signatureFile = plan.projectDir / ".vix-config.sig";
    plan.configureLogPath = plan.projectDir / "configure.log";
    plan.buildLogPath = plan.projectDir / "build.log";
    plan.targetName = plan.exeName;

    plan.useVixRuntime = probe.usesVixRuntime || script_uses_vix(plan.scriptPath);

    plan.exePath = plan.buildDir / plan.exeName;
#ifdef _WIN32
    plan.exePath += ".exe";
#endif

    plan.configSignature = make_script_config_signature(
        plan.useVixRuntime,
        opt.enableSanitizers,
        opt.enableUbsanOnly,
        opt.enableThreadSanitizer,
        opt.scriptFlags,
        opt.withSqlite,
        opt.withMySql);

    plan.shouldConfigure = true;
    plan.shouldBuild = true;
    plan.shouldRun = true;
    plan.passthroughRuntime = false;
    plan.effectiveTimeoutSec = effective_timeout_sec(opt);

    std::error_code ec;
    fs::create_directories(plan.scriptsRoot, ec);
    fs::create_directories(plan.projectDir, ec);

    return plan;
  }

  int run_single_cpp_cmake(const Options &opt, const CMakeScriptPlan &plan)
  {
    Options o = opt;
    ScriptProjectState state = make_state_from_cmake_plan(plan);

    const int code = configure_and_build_script(o, state);

    if (code == 130)
      return 0;

    if (code != 0)
      return code;

#ifdef _WIN32
    return run_script_binary_windows(o, state);
#else
    return run_script_binary_posix(o, state);
#endif
  }

  int run_single_cpp(const Options &opt)
  {
    Options o = opt;

    const int prepCode = prepare_script_options_common(o);
    if (prepCode != 0)
      return prepCode;

    const ScriptProbeResult probe = probe_single_cpp_script(o);

    if (script_can_use_direct_compile(probe))
    {
      const DirectScriptPlan directPlan = make_direct_script_plan(o, probe);
      return run_single_cpp_direct(o, directPlan);
    }

    const CMakeScriptPlan cmakePlan = make_cmake_script_plan(o, probe);
    return run_single_cpp_cmake(o, cmakePlan);
  }

  int build_script_executable(const Options &opt, std::filesystem::path &exePath)
  {
    return build_script_executable_internal(opt, exePath);
  }

  int run_single_cpp_watch(const Options &opt)
  {
    using namespace std::chrono_literals;
    namespace fs = std::filesystem;

#ifndef _WIN32
    using Clock = std::chrono::steady_clock;
#endif

    const fs::path script = opt.cppFile;
    if (!fs::exists(script))
    {
      error("C++ file not found: " + script.string());
      return 1;
    }

    std::error_code ec{};
    auto lastWrite = fs::last_write_time(script, ec);
    if (ec)
    {
      error("Unable to read last_write_time for: " + script.string());
      return 1;
    }

    const bool usesVixRuntime = script_uses_vix(script);
    const bool hasForceServer = opt.forceServerLike;
    const bool hasForceScript = opt.forceScriptLike;
    bool dynamicServerLike = usesVixRuntime;

    auto final_is_server = [&](bool runtimeGuess) -> bool
    {
      if (hasForceServer && hasForceScript)
        return true;
      if (hasForceServer)
        return true;
      if (hasForceScript)
        return false;
      return runtimeGuess;
    };

    auto kind_label = [&](bool runtimeGuess) -> std::string
    {
      return final_is_server(runtimeGuess) ? "dev server" : "script";
    };

    hint("Watching: " + script.string());

#ifdef _WIN32
    while (true)
    {
      const auto start = std::chrono::steady_clock::now();
      int code = run_single_cpp(opt);
      const auto end = std::chrono::steady_clock::now();
      const auto ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

      if (!hasForceServer && !hasForceScript)
      {
        bool runtimeGuess = dynamicServerLike;
        const bool longLived = (ms >= 500);

        if (longLived && !runtimeGuess)
          runtimeGuess = true;
        if (!longLived && runtimeGuess && code == 0)
          runtimeGuess = false;

        dynamicServerLike = runtimeGuess;
      }

      if (code != 0)
      {
        const std::string label = kind_label(dynamicServerLike);
        error("Last " + label + " run failed (exit code " + std::to_string(code) + ").");
        hint("Fix the errors, save the file, and Vix will rebuild automatically.");
      }

      for (;;)
      {
        std::this_thread::sleep_for(500ms);

        auto nowWrite = fs::last_write_time(script, ec);
        if (ec)
        {
          error("Error reading last_write_time during watch loop.");
          return 1;
        }

        if (nowWrite != lastWrite)
        {
          lastWrite = nowWrite;
          print_watch_restart_banner(script, "Rebuilding script...");
          break;
        }
      }
    }

    return 0;
#else
    while (true)
    {
      fs::path exePath;
      int buildCode = build_script_executable(opt, exePath);
      if (buildCode != 0)
      {
        watch_spinner_stop();

        const std::string label = kind_label(dynamicServerLike);
        error("Last " + label + " build failed (exit code " + std::to_string(buildCode) + ").");
        hint("Fix the errors, save the file, and Vix will rebuild automatically.");

        for (;;)
        {
          std::this_thread::sleep_for(500ms);

          auto nowWrite = fs::last_write_time(script, ec);
          if (ec)
          {
            error("Error reading last_write_time during watch loop.");
            return 1;
          }

          if (nowWrite != lastWrite)
          {
            lastWrite = nowWrite;
            print_watch_restart_banner(script, "Rebuilding script...");
            break;
          }
        }
        continue;
      }

      const auto childStart = Clock::now();

      int gate[2] = {-1, -1};
      if (::pipe(gate) != 0)
      {
        error("Failed to create restart gate pipe.");
        return 1;
      }

      pid_t pid = fork();
      if (pid < 0)
      {
        error("Failed to fork() for dev process.");
        ::close(gate[0]);
        ::close(gate[1]);
        return 1;
      }

      if (pid == 0)
      {
        ::close(gate[1]);

        char b = 0;
        const ssize_t n = ::read(gate[0], &b, 1);
        if (n < 0)
        {
          std::cerr << "[vix][dev] gate read failed: " << std::strerror(errno) << "\n";
          ::close(gate[0]);
          _exit(127);
        }
        ::close(gate[0]);

        ::setenv("VIX_STDOUT_MODE", "line", 1);
        ::setenv("VIX_MODE", "dev", 1);

        apply_sanitizer_env_if_needed(
            opt.enableSanitizers,
            opt.enableUbsanOnly,
            opt.enableThreadSanitizer);

        if (!opt.cwd.empty())
        {
          const std::string cwd = normalize_cwd_if_needed(opt.cwd);
          if (::chdir(cwd.c_str()) != 0)
            _exit(127);
        }

        std::vector<std::string> argvStr;
        argvStr.push_back(exePath.string());
        for (const auto &a : opt.runArgs)
        {
          if (!a.empty())
            argvStr.push_back(a);
        }

        std::vector<char *> argv;
        argv.reserve(argvStr.size() + 1);
        for (auto &s : argvStr)
          argv.push_back(const_cast<char *>(s.c_str()));
        argv.push_back(nullptr);

        ::execv(argv[0], argv.data());
        _exit(127);
      }

      ::close(gate[0]);

      bool needRestart = false;
      bool running = true;

      watch_spinner_stop();

      {
        const bool isServer = final_is_server(dynamicServerLike);
        const std::string kind = isServer ? "Dev server" : "Script";
        info(std::string("🏃 ") + kind + " started (pid=" + std::to_string(pid) + ")");
      }

      const ssize_t w = ::write(gate[1], "1", 1);
      if (w < 0)
        error(std::string("restart gate write failed: ") + std::strerror(errno));

      ::close(gate[1]);

      while (running)
      {
        std::this_thread::sleep_for(300ms);

        auto nowWrite = fs::last_write_time(script, ec);
        if (!ec && nowWrite != lastWrite)
        {
          lastWrite = nowWrite;
          print_watch_restart_banner(script, "Rebuilding script...");
          needRestart = true;

          if (kill(pid, SIGINT) != 0)
          {
          }
        }

        int status = 0;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
        {
          running = false;

          const auto childEnd = Clock::now();
          const auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(childEnd - childStart).count();

          int exitCode = 0;
          if (WIFEXITED(status))
            exitCode = WEXITSTATUS(status);
          else if (WIFSIGNALED(status))
            exitCode = 128 + WTERMSIG(status);

          if (!hasForceServer && !hasForceScript)
          {
            bool runtimeGuess = dynamicServerLike;
            const bool longLived = (ms >= 500);

            if (longLived && !runtimeGuess)
              runtimeGuess = true;
            if (!longLived && runtimeGuess && exitCode == 0)
              runtimeGuess = false;

            dynamicServerLike = runtimeGuess;
          }

          const bool isServer = final_is_server(dynamicServerLike);
          const std::string label = isServer ? "dev server" : "script";

          if (needRestart)
            break;

          if (exitCode != 0)
          {
            error(label + " exited with code " +
                  std::to_string(exitCode) +
                  " (lifetime ~" + std::to_string(ms) + "ms).");
          }

          for (;;)
          {
            std::this_thread::sleep_for(500ms);

            auto now2 = fs::last_write_time(script, ec);
            if (ec)
            {
              error("Error reading last_write_time during post-exit watch.");
              return exitCode;
            }

            if (now2 != lastWrite)
            {
              lastWrite = now2;
              print_watch_restart_banner(script, "Rebuilding script...");
              break;
            }
          }

          break;
        }
      }
    }

    return 0;
#endif
  }

  int run_project_watch(const Options &opt, const std::filesystem::path &projectDir)
  {
#ifndef _WIN32

    using Clock = std::chrono::steady_clock;
    namespace fs = std::filesystem;

    const bool devMode = opt.devMode;
    const fs::path buildDir = projectDir / (devMode ? "build-ninja" : "build-dev");
    const std::string targetName = projectDir.filename().string();

    if (devMode)
    {
      vix::commands::RunCommand::dev::DevSession session(
          vix::commands::RunCommand::dev::DevSessionOptions{
              projectDir,
              buildDir,
              targetName,
              opt,
              std::chrono::milliseconds(300),
              std::chrono::milliseconds(200),
              opt.quiet});

      const auto result = session.run();
      return result.exitCode;
    }

    if (devMode)
    {
      info("Vix dev mode enabled.");
      hint("Project: " + projectDir.string());
      hint("Build directory: " + buildDir.string());
    }

    std::error_code ec;
    fs::create_directories(buildDir, ec);
    if (ec)
    {
      error("Unable to create dev build directory: " + buildDir.string() +
            " (" + ec.message() + ")");
      return 1;
    }

    auto compute_timestamp = [&](std::error_code &outEc) -> fs::file_time_type
    {
      using ftime = fs::file_time_type;
      outEc.clear();

      ftime latest = ftime::min();

      auto should_skip_dir = [](const fs::path &p) -> bool
      {
        const std::string name = p.filename().string();

        return name == ".git" ||
               name == ".vix" ||
               name == "build" ||
               name == "build-dev" ||
               name == "build-ninja" ||
               name == "build-release" ||
               name == "node_modules" ||
               name == ".cache" ||
               name == ".idea" ||
               name == ".vscode";
      };

      auto should_watch_file = [](const fs::path &p) -> bool
      {
        const std::string name = p.filename().string();
        const std::string ext = p.extension().string();

        if (name == "CMakeLists.txt" ||
            name == "CMakePresets.json" ||
            name == "vix.json" ||
            name == "vix.toml" ||
            name == "vix.lock")
        {
          return true;
        }

        return ext == ".cpp" ||
               ext == ".cc" ||
               ext == ".cxx" ||
               ext == ".c" ||
               ext == ".hpp" ||
               ext == ".hh" ||
               ext == ".hxx" ||
               ext == ".h" ||
               ext == ".ipp" ||
               ext == ".cmake";
      };

      auto touch = [&](const fs::path &p)
      {
        std::error_code e;

        if (!fs::exists(p, e) || e)
          return;

        if (!fs::is_regular_file(p, e) || e)
          return;

        auto t = fs::last_write_time(p, e);
        if (!e && t > latest)
          latest = t;
      };

      for (auto it = fs::recursive_directory_iterator(
               projectDir,
               fs::directory_options::skip_permission_denied,
               outEc);
           !outEc && it != fs::recursive_directory_iterator();
           ++it)
      {
        const fs::path p = it->path();

        if (it->is_directory())
        {
          if (should_skip_dir(p))
            it.disable_recursion_pending();

          continue;
        }

        if (!it->is_regular_file())
          continue;

        if (!should_watch_file(p))
          continue;

        touch(p);
      }

      return latest;
    };

    std::error_code tsEc;
    auto lastStamp = compute_timestamp(tsEc);
    if (tsEc)
      hint("Unable to compute initial timestamp for dev watch: " + tsEc.message());

    if (!opt.quiet)
    {
      std::cout << CYAN << BOLD << "Dev " << RESET
                << CYAN << BOLD << targetName << RESET
                << GRAY << " (dev)" << RESET
                << "\n";

      std::cout << "  "
                << GRAY << "watching: " << RESET
                << projectDir.string()
                << "\n";

      std::cout << "  "
                << GRAY << "target  : " << RESET
                << targetName
                << "\n";

      std::cout << "  "
                << GRAY << "press Ctrl+C to stop" << RESET
                << "\n\n";
    }

    while (true)
    {
      if (!has_cmake_cache(buildDir))
      {
        if (devMode)
          info("Configuring project for dev mode (build-ninja/).");
        else
          info("Configuring project for watch mode (build-dev/).");

        std::ostringstream oss;

        if (devMode)
        {
          oss << "cmake"
              << " -S " << quote(projectDir.string())
              << " -B " << quote(buildDir.string())
              << " -G Ninja"
              << " -DCMAKE_BUILD_TYPE=Debug"
              << " -DCMAKE_EXPORT_COMPILE_COMMANDS=ON";
        }
        else
        {
          oss << "cd " << quote(buildDir.string()) << " && cmake ..";
        }

        const std::string cmd = oss.str();

        const int code = run_cmd_live_filtered(cmd, "Configuring project");
        if (code != 0)
        {
          const std::string label = devMode ? "build-ninja/" : "build-dev/";

          error("CMake configure failed for dev mode (" + label + ", code " + std::to_string(code) + ").");
          hint("Check your CMakeLists.txt or run the command manually:");

          if (devMode)
          {
            step("  cmake -S " + projectDir.string() + " -B " + buildDir.string() + " -G Ninja");
          }
          else
          {
            step("  cd " + buildDir.string());
            step("  cmake ..");
          }

          return code != 0 ? code : 4;
        }

        if (!opt.quiet)
          vix::cli::util::ok_line(std::cout, "Configured");
      }

      {
        if (!opt.quiet)
        {
          std::cout << CYAN << BOLD << "Compiling " << RESET
                    << CYAN << BOLD << targetName << RESET
                    << GRAY << " (dev)" << RESET
                    << "\n";
        }

        watch_spinner_start("Building project...");

        std::ostringstream oss;

        if (devMode)
        {
          oss << "cmake --build " << quote(buildDir.string())
              << " --target " << quote(targetName);
        }
        else
        {
          oss << "cd " << quote(buildDir.string()) << " && cmake --build .";
        }

        if (fs::exists(buildDir / "build.ninja"))
        {
          oss << " --";

          if (opt.jobs > 0)
            oss << " -j " << opt.jobs;

          oss << " --quiet";
        }
        else
        {
          if (opt.jobs > 0)
            oss << " -j " << opt.jobs;
        }

        const std::string cmd = oss.str();

        int code = 0;
        std::string buildLog = run_and_capture_with_code(cmd + " 2>&1", code);
        code = normalize_exit_code(code);

        if (code != 0)
        {
          watch_spinner_stop();

          const std::string buildLabel = devMode ? "build-ninja/" : "build-dev/";

          if (!buildLog.empty())
          {
            (void)vix::cli::ErrorHandler::printBuildErrors(
                buildLog,
                buildDir,
                "Build failed in dev mode (" + buildLabel + ")");
          }
          else
          {
            error("Build failed in dev mode (" + buildLabel + ", code " + std::to_string(code) + ").");
          }

          hint("Fix the errors, save your files, and Vix will rebuild automatically.");

          while (true)
          {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            std::error_code loopEc;
            auto nowStamp = compute_timestamp(loopEc);
            if (!loopEc && nowStamp != lastStamp)
            {
              lastStamp = nowStamp;
              print_watch_restart_banner(projectDir, "Rebuilding project...");
              break;
            }
          }

          continue;
        }

        watch_spinner_finish();
      }

      const std::string exeName = projectDir.filename().string();
      fs::path exePath = buildDir / exeName;

      if (!fs::exists(exePath))
      {
        error("Dev executable not found in build-dev/: " + exePath.string());
        hint("Make sure your CMakeLists.txt defines an executable named '" + exeName + "'.");
        return 1;
      }

      const auto childStart = Clock::now();

      pid_t pid = ::fork();
      if (pid < 0)
      {
        error("Failed to fork() for dev process.");
        return 1;
      }

      if (pid == 0)
      {
        const std::string runCwd = opt.cwd.empty()
                                       ? projectDir.string()
                                       : normalize_cwd_if_needed(opt.cwd);

        if (::chdir(runCwd.c_str()) != 0)
        {
          std::cerr << "[vix][run] chdir failed: " << std::strerror(errno) << "\n";
          _exit(127);
        }

        if (::setenv("VIX_STDOUT_MODE", "line", 1) != 0)
        {
          std::cerr << "[vix][run] setenv failed: " << std::strerror(errno) << "\n";
          _exit(127);
        }

        if (::setenv("VIX_MODE", "dev", 1) != 0)
        {
          std::cerr << "[vix][run] setenv VIX_MODE failed: " << std::strerror(errno) << "\n";
          _exit(127);
        }

        if (opt.withMySql)
        {
          ::setenv("VIX_DB_ENGINE", "mysql", 1);
          ::setenv("VIX_ENABLE_DB", "1", 1);
          ::setenv("VIX_DB_USE_MYSQL", "1", 1);
        }

        if (opt.withSqlite)
        {
          ::setenv("VIX_DB_ENGINE", "sqlite", 1);
          ::setenv("VIX_ENABLE_DB", "1", 1);
          ::setenv("VIX_DB_USE_SQLITE", "1", 1);
        }

        const std::string exeStr = exePath.string();
        const char *argv0 = exeStr.c_str();

        ::execl(argv0, argv0, (char *)nullptr);

        std::cerr << "[vix][run] execl failed: " << std::strerror(errno) << "\n";
        _exit(127);
      }

      watch_spinner_pause_for_output();
      if (dev_verbose_ui(opt))
        success("PID " + std::to_string(pid));

      bool needRestart = false;
      bool running = true;

      while (running)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        std::error_code loopEc;
        auto nowStamp = compute_timestamp(loopEc);
        if (!loopEc && nowStamp != lastStamp)
        {
          lastStamp = nowStamp;
          print_watch_restart_banner(projectDir, "Rebuilding project...");
          needRestart = true;

          if (::kill(pid, SIGINT) != 0)
          {
          }
        }

        int status = 0;
        pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid)
        {
          running = false;

          const auto childEnd = Clock::now();
          const auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(childEnd - childStart).count();

          int exitCode = 0;
          if (WIFEXITED(status))
            exitCode = WEXITSTATUS(status);
          else if (WIFSIGNALED(status))
            exitCode = 128 + WTERMSIG(status);

          if (!needRestart)
          {
            if (exitCode != 0)
            {
              error("Dev server exited with code " +
                    std::to_string(exitCode) +
                    " (lifetime ~" + std::to_string(ms) + "ms).");
            }
            else
            {
              success("Dev server stopped cleanly (lifetime ~" +
                      std::to_string(ms) + "ms).");
            }
            return exitCode;
          }
        }
      }
    }

    return 0;
#else
    (void)opt;
    (void)projectDir;
    error("run_project_watch is not implemented on Windows.");
    return 1;
#endif
  }

} // namespace vix::commands::RunCommand::detail
