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
#include <vix/cli/commands/run/detail/ScriptCMake.hpp>
#include <vix/cli/errors/RawLogDetectors.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>

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

    ScriptProjectState prepare_script_project_state(Options &opt)
    {
      ScriptProjectState state;
      state.script = fs::absolute(opt.cppFile).lexically_normal();
      state.exeName = state.script.stem().string();
      state.scriptsRoot = get_scripts_root();

      const std::string scriptCacheKey = hex_u64(
          fnv1a_64("script-cache:" + state.script.string()));

      state.projectDir = state.scriptsRoot / scriptCacheKey;
      state.cmakeLists = state.projectDir / "CMakeLists.txt";
      state.buildDir = state.projectDir / "build-ninja";
      state.sigFile = state.projectDir / ".vix-config.sig";
      state.configureLogPath = state.projectDir / "configure.log";
      state.buildLogPath = state.projectDir / "build.log";
      state.useVixRuntime = script_uses_vix(state.script);
      state.exePath = state.buildDir / state.exeName;
#ifdef _WIN32
      state.exePath += ".exe";
#endif

      fs::create_directories(state.scriptsRoot);
      fs::create_directories(state.projectDir);

      {
        std::ofstream ofs(state.cmakeLists);
        ofs << make_script_cmakelists(
            state.exeName,
            state.script,
            state.useVixRuntime,
            opt.scriptFlags,
            opt.withSqlite,
            opt.withMySql);
      }

      state.configSignature = make_script_config_signature(
          state.useVixRuntime,
          opt.enableSanitizers,
          opt.enableUbsanOnly,
          opt.scriptFlags);

      return state;
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

      if (want_sanitizers(opt.enableSanitizers, opt.enableUbsanOnly))
      {
        oss << " -DVIX_ENABLE_SANITIZERS=ON"
            << " -DVIX_SANITIZER_MODE="
            << sanitizer_mode_string(opt.enableSanitizers, opt.enableUbsanOnly);
      }
      else
      {
        oss << " -DVIX_ENABLE_SANITIZERS=OFF";
      }

      oss << " >" << quote(state.configureLogPath.string()) << " 2>&1";

      const std::string cmd = oss.str();
      int code = std::system(cmd.c_str());
      code = normalize_exit_code(code);

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
        std::cout << logContent << "\n";
        handled = true;
      }

      error("Script configure failed.");
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
      apply_sanitizer_env_if_needed(opt.enableSanitizers, opt.enableUbsanOnly);

      const bool isPlainScript = !state.useVixRuntime;
      std::string cmdRun = "VIX_STDOUT_MODE=line " + quote(state.exePath.string());
      cmdRun += join_quoted_args_local(opt.runArgs);
      cmdRun = wrap_with_cwd_if_needed(opt, cmdRun);

      LiveRunResult rr = run_cmd_live_filtered_capture(
          cmdRun,
          "",
          isPlainScript,
          effective_timeout_sec(opt));

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
        hint("ℹ Server interrupted by user (SIGINT).");
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

          if (!handled)
          {
            handled = vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
                runtimeLog,
                state.script,
                "Script execution failed");
          }

          if (!handled &&
              vix::cli::errors::RawLogDetectors::handleKnownRunFailure(runtimeLog, state.script))
          {
            handled = true;
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

      const LiveRunResult rr = run_cmd_live_filtered_capture(
          cmdRun,
          "",
          true,
          effective_timeout_sec(opt));

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

    int configure_and_build_script(Options &o, ScriptProjectState &state)
    {
      const int cfgCode = configure_script_project(o, state);
      if (cfgCode != 0)
        return cfgCode;

      state.skipBuild = can_skip_build(o, state);

      const int buildCode = build_script_project(o, state);
      if (buildCode != 0)
        return buildCode;

      return ensure_script_executable_exists(state);
    }

    int prepare_script_state_from_options(Options &o, ScriptProjectState &state)
    {
      print_double_dash_warning_if_needed(o);

      if (!ensure_script_exists(o.cppFile))
        return 1;

      if (o.autoDeps != AutoDepsMode::None)
        apply_auto_deps_includes_from_deps_folder(o, o.cppFile.parent_path());

      state = prepare_script_project_state(o);
      compute_need_configure(state);
      return 0;
    }

    int build_script_executable_internal(const Options &opt, fs::path &exePath)
    {
      Options o = opt;
      ScriptProjectState state;

      const int prepCode = prepare_script_state_from_options(o, state);
      if (prepCode != 0)
        return prepCode;

      if (o.clean)
      {
        std::error_code ec;
        fs::remove_all(state.buildDir, ec);
        fs::remove(state.sigFile, ec);
        state.needConfigure = true;
      }

      const int code = configure_and_build_script(o, state);
      if (code != 0)
        return code;

      exePath = state.exePath;
      return 0;
    }

  } // namespace

  int run_single_cpp(const Options &opt)
  {
    Options o = opt;
    ScriptProjectState state;

    const int prepCode = prepare_script_state_from_options(o, state);
    if (prepCode != 0)
      return prepCode;

    if (o.clean)
    {
      std::error_code ec;
      fs::remove_all(state.buildDir, ec);
      fs::remove(state.sigFile, ec);
      state.needConfigure = true;
    }

    const int code = configure_and_build_script(o, state);
    if (code != 0)
      return code;

#ifdef _WIN32
    return run_script_binary_windows(o, state);
#else
    return run_script_binary_posix(o, state);
#endif
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

        apply_sanitizer_env_if_needed(opt.enableSanitizers, opt.enableUbsanOnly);

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

    const fs::path buildDir = projectDir / "build-dev";

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

      auto touch = [&](const fs::path &p)
      {
        std::error_code e;
        if (!fs::exists(p, e) || e)
          return;
        auto t = fs::last_write_time(p, e);
        if (!e && t > latest)
          latest = t;
      };

      touch(projectDir / "CMakeLists.txt");

      fs::path srcDir = projectDir / "src";
      if (fs::exists(srcDir, outEc) && !outEc)
      {
        for (auto it = fs::recursive_directory_iterator(
                 srcDir,
                 fs::directory_options::skip_permission_denied,
                 outEc);
             !outEc && it != fs::recursive_directory_iterator();
             ++it)
        {
          if (it->is_regular_file())
            touch(it->path());
        }
      }

      return latest;
    };

    std::error_code tsEc;
    auto lastStamp = compute_timestamp(tsEc);
    if (tsEc)
      hint("Unable to compute initial timestamp for dev watch: " + tsEc.message());

    info("Watcher Process started (project hot reload).");
    hint("Watching project: " + projectDir.string());
    hint("Press Ctrl+C to stop dev mode.");

    while (true)
    {
      if (!has_cmake_cache(buildDir))
      {
        info("Configuring project for dev mode (build-dev/).");

        std::ostringstream oss;
        oss << "cd " << quote(buildDir.string()) << " && cmake ..";
        const std::string cmd = oss.str();

        const int code = run_cmd_live_filtered(cmd, "Configuring project (dev mode)");
        if (code != 0)
        {
          error("CMake configure failed for dev mode (build-dev/, code " + std::to_string(code) + ").");
          hint("Check your CMakeLists.txt or run the command manually:");
          step("  cd " + buildDir.string());
          step("  cmake ..");
          return code != 0 ? code : 4;
        }

        if (dev_verbose_ui(opt))
          success("Dev configure completed (build-dev/).");
      }

      {
        watch_spinner_start("Rebuilding project...");

        std::ostringstream oss;
        oss << "cd " << quote(buildDir.string()) << " && cmake --build .";

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

        watch_spinner_pause_for_output();

        if (code != 0)
        {
          if (!buildLog.empty())
          {
            (void)vix::cli::ErrorHandler::printBuildErrors(
                buildLog,
                buildDir,
                "Build failed in dev mode (build-dev/)");
          }
          else
          {
            error("Build failed in dev mode (build-dev/, code " + std::to_string(code) + ").");
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

        if (dev_verbose_ui(opt))
          success("Build completed (dev mode).");
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
        if (::chdir(buildDir.string().c_str()) != 0)
        {
          std::cerr << "[vix][run] chdir failed: " << std::strerror(errno) << "\n";
          _exit(127);
        }

        if (::setenv("VIX_STDOUT_MODE", "line", 1) != 0)
        {
          std::cerr << "[vix][run] setenv failed: " << std::strerror(errno) << "\n";
          _exit(127);
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
