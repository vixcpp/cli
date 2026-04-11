/**
 *
 *  @file RunDetail.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_RUN_DETAIL_HPP
#define VIX_RUN_DETAIL_HPP

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <vix/cli/ErrorHandler.hpp>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace vix::commands::RunCommand::detail
{
  namespace fs = std::filesystem;

  /**
   * @brief Automatic dependency discovery strategy for script mode.
   */
  enum class AutoDepsMode
  {
    None,
    Local,
    Up
  };

  /**
   * @brief High-level kind of input handled by `vix run`.
   */
  enum class RunInputKind
  {
    None,
    Project,
    SingleCpp,
    Manifest
  };

  /**
   * @brief Parsing state and user-facing options for `vix run`.
   *
   * This structure intentionally stays close to the current CLI behavior.
   * It stores raw parse results and feature toggles before execution planning.
   */
  struct Options
  {
    // -------------------------------------------------------------------------
    // Input selection
    // -------------------------------------------------------------------------
    std::string appName;
    std::string exampleName;

    bool singleCpp = false;
    fs::path cppFile;

    bool manifestMode = false;
    fs::path manifestFile;

    // Project/build selection
    std::string preset = "dev-ninja";
    std::string runPreset;
    std::string dir;
    int jobs = 0;
    bool clean = false;

    // Output / UI
    bool quiet = false;
    bool verbose = false;
    std::string logLevel;
    std::string logFormat;
    std::string logColor; // auto|always|never
    bool noColor = false;
    std::string clearMode = "auto";

    // Behavior switches
    bool watch = false;
    AutoDepsMode autoDeps = AutoDepsMode::None;

    bool forceServerLike = false;
    bool forceScriptLike = false;

    bool enableSanitizers = false; // ASan + UBSan
    bool enableUbsanOnly = false;  // UBSan only

    bool withSqlite = false;
    bool withMySql = false;

    std::optional<bool> docs;

    // Script / runtime forwarding
    std::vector<std::string> scriptFlags;
    std::vector<std::string> runArgs;
    std::vector<std::string> runEnv;

    int timeoutSec = 0;
    std::string cwd;

    // Parse diagnostics / separators
    bool parseFailed = false;
    int parseExitCode = 0;

    bool hasDoubleDash = false;
    std::vector<std::string> doubleDashArgs;

    bool hasRunSeparator = false;
    std::vector<std::string> runArgsAfterRun;

    bool badDoubleDashRuntimeArgs = false;
    std::string badDoubleDashArg;

    bool warnedVixFlagAfterDoubleDash = false;
    std::string warnedArg;
  };

  /**
   * @brief Resolved execution context derived from parsed options and filesystem inspection.
   *
   * This is the first step toward a cleaner `vix run` architecture:
   * parse first, resolve context second, execute third.
   */
  struct RunContext
  {
    RunInputKind inputKind = RunInputKind::None;

    fs::path cwd;
    fs::path projectDir;
    fs::path buildDir;

    fs::path cppFile;
    fs::path manifestFile;

    std::string configurePreset;
    std::string resolvedRunPreset;
    std::string targetName;

    bool hasCMakeLists = false;
    bool hasPresets = false;
    bool hasBuildCache = false;
    bool useAutoDeps = false;
  };

  /**
   * @brief Concrete execution plan for a `vix run` invocation.
   */
  struct RunPlan
  {
    RunContext context;

    bool shouldConfigure = true;
    bool shouldBuild = true;
    bool shouldRun = true;

    bool passthroughRuntime = false;
    int effectiveTimeoutSec = 0;

    std::string configureCmd;
    std::string buildCmd;
    std::string runCmd;
  };

  /**
   * @brief Result returned by script-mode execution helpers.
   */
  struct ScriptRunResult
  {
    int code = 0;
    bool handled = false;
  };

  /**
   * @brief Result of a captured live process execution.
   */
  struct LiveRunResult
  {
    int rawStatus = 0;
    int exitCode = 0;

    std::string stdoutText;
    std::string stderrText;

    bool failureHandled = false;
    bool printed_live = false;

    bool terminatedBySignal = false;
    int termSignal = 0;
  };

  /**
   * @brief Small cache stamp used to avoid unnecessary rebuilds.
   */
  struct RebuildCacheStamp
  {
    std::uint64_t exe_mtime_ns = 0;
    std::uint64_t depfiles_fingerprint = 0;
    std::uint64_t max_dep_mtime_ns = 0;
  };

  // ===========================================================================
  // Parsing / context resolution
  // ===========================================================================

  /**
   * @brief Parse CLI arguments for `vix run`.
   */
  Options parse(const std::vector<std::string> &args);

  /**
   * @brief Resolve the main input kind from parsed options.
   */
  inline RunInputKind detect_input_kind(const Options &opt) noexcept
  {
    if (opt.singleCpp)
      return RunInputKind::SingleCpp;

    if (opt.manifestMode)
      return RunInputKind::Manifest;

    if (!opt.appName.empty() || !opt.dir.empty())
      return RunInputKind::Project;

    return RunInputKind::Project;
  }

  /**
   * @brief Compute the effective timeout for the current run.
   */
  inline int effective_timeout_sec(const Options &opt) noexcept
  {
    if (opt.forceServerLike || opt.watch)
      return 0;

    return opt.timeoutSec;
  }

  /**
   * @brief Normalize a cwd string to an absolute path when possible.
   */
  inline std::string normalize_cwd_if_needed(const std::string &cwd)
  {
    if (cwd.empty())
      return {};

    std::error_code ec{};
    fs::path p(cwd);

    if (p.is_relative())
      p = fs::absolute(p, ec);

    if (ec)
      return cwd;

    return p.string();
  }

  /**
   * @brief Resolve the entry C++ file referenced by a .vix manifest.
   */
  fs::path manifest_entry_cpp(const fs::path &manifestFile);

  /**
   * @brief Select the project directory from parsed options and current directory.
   */
  std::optional<fs::path> choose_project_dir(
      const Options &opt,
      const fs::path &cwd);

  // ===========================================================================
  // Process / command execution
  // ===========================================================================

  /**
   * @brief Normalize a platform-specific process exit status to a simple exit code.
   */
  inline int normalize_exit_code(int code) noexcept
  {
#ifdef _WIN32
    return code;
#else
    if (code < 0)
      return 1;

    if (code <= 255)
      return code;

    if (WIFEXITED(code))
      return WEXITSTATUS(code);

    if (WIFSIGNALED(code))
      return 128 + WTERMSIG(code);

    return 1;
#endif
  }

  /**
   * @brief Run a command live with filtering.
   */
  int run_cmd_live_filtered(
      const std::string &cmd,
      const std::string &spinnerLabel = {});

  /**
   * @brief Run a command live, capture its output, and optionally passthrough runtime output.
   */
  LiveRunResult run_cmd_live_filtered_capture(
      const std::string &cmd,
      const std::string &spinnerLabel,
      bool passthroughRuntime,
      int timeoutSec = 0);

  /**
   * @brief Run a command and capture its output plus exit code.
   */
  std::string run_and_capture_with_code(const std::string &cmd, int &exitCode);

  /**
   * @brief Run a command and capture its output.
   */
  std::string run_and_capture(const std::string &cmd);

  /**
   * @brief Handle final runtime exit reporting.
   */
  void handle_runtime_exit_code(
      int code,
      const std::string &context,
      bool alreadyHandled);

  // ===========================================================================
  // Script mode
  // ===========================================================================

  /**
   * @brief Return the root directory used for generated script projects.
   */
  fs::path get_scripts_root();

  /**
   * @brief Detect whether a .cpp script uses the Vix runtime.
   */
  bool script_uses_vix(const fs::path &cppPath);

  /**
   * @brief Generate the CMakeLists.txt content used by script mode.
   */
  std::string make_script_cmakelists(
      const std::string &exeName,
      const fs::path &cppPath,
      bool useVixRuntime,
      const std::vector<std::string> &scriptFlags,
      bool withSqlite,
      bool withMySql);

  /**
   * @brief Execute a single C++ file with the script pipeline.
   */
  int run_single_cpp(const Options &opt);

  /**
   * @brief Execute a single C++ file in watch mode.
   */
  int run_single_cpp_watch(const Options &opt);

  /**
   * @brief Execute a project in watch mode.
   */
  int run_project_watch(const Options &opt, const fs::path &projectDir);

  // ===========================================================================
  // Build / preset helpers
  // ===========================================================================

  /**
   * @brief Quote a shell argument.
   */
  std::string quote(const std::string &s);

  /**
   * @brief Build a configure command for CMake.
   */
  inline std::string cmake_configure_cmd(
      const fs::path &projectDir,
      const std::string &configurePreset,
      const fs::path &buildDir)
  {
#ifdef _WIN32
    if (!configurePreset.empty())
    {
      return "cmd /C \"cd /D " + quote(projectDir.string()) +
             " && cmake --preset " + quote(configurePreset) + "\"";
    }

    return "cmd /C \"cd /D " + quote(projectDir.string()) +
           " && cmake -S . -B " + quote(buildDir.string()) + " -G Ninja\"";
#else
    if (!configurePreset.empty())
    {
      return "cd " + quote(projectDir.string()) +
             " && cmake --preset " + quote(configurePreset);
    }

    return "cd " + quote(projectDir.string()) +
           " && cmake -S . -B " + quote(buildDir.string()) + " -G Ninja";
#endif
  }

  /**
   * @brief Return whether the project defines CMake presets.
   */
  bool has_presets(const fs::path &projectDir);

  /**
   * @brief Choose the run preset associated with a configure preset.
   */
  std::string choose_run_preset(
      const fs::path &dir,
      const std::string &configurePreset,
      const std::string &userRunPreset);

  /**
   * @brief Return whether a CMake cache exists in the given build directory.
   */
  bool has_cmake_cache(const fs::path &buildDir);

  /**
   * @brief Resolve the binary directory from a CMake preset, if any.
   */
  std::optional<fs::path> preset_binary_dir(
      const fs::path &projectDir,
      const std::string &configurePreset);

  /**
   * @brief Choose a configure preset using the current smart preset logic.
   */
  std::string choose_configure_preset_smart(
      const fs::path &projectDir,
      const std::string &userPreset);

  /**
   * @brief Resolve the build directory using the current smart logic.
   */
  fs::path resolve_build_dir_smart(
      const fs::path &projectDir,
      const std::string &configurePreset);

  // ===========================================================================
  // Logging / environment helpers
  // ===========================================================================

  /**
   * @brief Apply the log level environment expected by the runtime.
   */
  void apply_log_level_env(const Options &opt);

  /**
   * @brief Apply the log format environment expected by the runtime.
   */
  void apply_log_format_env(const Options &opt);

  /**
   * @brief Apply the log color environment expected by the runtime.
   */
  void apply_log_color_env(const Options &opt);

  /**
   * @brief Apply all runtime logging environment variables.
   */
  inline void apply_log_env(const Options &opt)
  {
    apply_log_level_env(opt);
    apply_log_format_env(opt);
    apply_log_color_env(opt);
  }

  /**
   * @brief Join arguments as shell-quoted tokens.
   */
  std::string join_quoted_args_local(const std::vector<std::string> &a);

  /**
   * @brief Wrap a command with cwd switching if needed.
   */
  std::string wrap_with_cwd_if_needed(const Options &opt, const std::string &cmd);

  // ===========================================================================
  // Build log analysis
  // ===========================================================================

  /**
   * @brief Return true when the build log indicates real compilation work.
   */
  bool has_real_build_work(const std::string &log);

  // ===========================================================================
  // Rebuild cache helpers
  // ===========================================================================

  /**
   * @brief Return the file modification time in nanoseconds, or 0 on failure.
   */
  inline std::uint64_t file_mtime_ns(const fs::path &p, std::error_code &ec)
  {
    ec.clear();
    const auto ft = fs::last_write_time(p, ec);
    if (ec)
      return 0;

    using namespace std::chrono;
    const auto ns = duration_cast<nanoseconds>(ft.time_since_epoch()).count();
    return (ns < 0) ? 0ull : static_cast<std::uint64_t>(ns);
  }

  /**
   * @brief Return the file size as u64, or 0 on failure.
   */
  inline std::uint64_t file_size_u64(const fs::path &p, std::error_code &ec)
  {
    ec.clear();
    const auto sz = fs::file_size(p, ec);
    if (ec)
      return 0;

    return static_cast<std::uint64_t>(sz);
  }

  /**
   * @brief Load a rebuild cache stamp from disk.
   */
  inline std::optional<RebuildCacheStamp> load_rebuild_cache_stamp(const fs::path &stampFile)
  {
    std::ifstream ifs(stampFile);
    if (!ifs)
      return std::nullopt;

    RebuildCacheStamp s{};
    std::string line;

    auto parse_u64 = [](const std::string &v) -> std::optional<std::uint64_t>
    {
      try
      {
        std::size_t idx = 0;
        unsigned long long x = std::stoull(v, &idx, 10);
        if (idx != v.size())
          return std::nullopt;

        return static_cast<std::uint64_t>(x);
      }
      catch (...)
      {
        return std::nullopt;
      }
    };

    while (std::getline(ifs, line))
    {
      const auto pos = line.find('=');
      if (pos == std::string::npos)
        continue;

      const std::string k = line.substr(0, pos);
      const std::string v = line.substr(pos + 1);

      auto u = parse_u64(v);
      if (!u)
        continue;

      if (k == "exe_mtime_ns")
        s.exe_mtime_ns = *u;
      else if (k == "depfiles_fingerprint")
        s.depfiles_fingerprint = *u;
      else if (k == "max_dep_mtime_ns")
        s.max_dep_mtime_ns = *u;
    }

    if (s.depfiles_fingerprint == 0 &&
        s.max_dep_mtime_ns == 0 &&
        s.exe_mtime_ns == 0)
    {
      return std::nullopt;
    }

    return s;
  }

  /**
   * @brief Save a rebuild cache stamp to disk.
   */
  inline void save_rebuild_cache_stamp(const fs::path &stampFile, const RebuildCacheStamp &s)
  {
    std::ofstream ofs(stampFile, std::ios::trunc);
    if (!ofs)
      return;

    ofs << "exe_mtime_ns=" << s.exe_mtime_ns << "\n";
    ofs << "depfiles_fingerprint=" << s.depfiles_fingerprint << "\n";
    ofs << "max_dep_mtime_ns=" << s.max_dep_mtime_ns << "\n";
  }

  /**
   * @brief Compute a fast fingerprint from a list of depfiles.
   */
  inline std::uint64_t depfiles_fingerprint_fast(const std::vector<fs::path> &depfiles)
  {
    std::uint64_t h = 1469598103934665603ull;

    auto fnv1a = [&](std::uint64_t v)
    {
      h ^= v;
      h *= 1099511628211ull;
    };

    std::error_code ec;
    for (const auto &d : depfiles)
    {
      fnv1a(std::hash<std::string>{}(d.string()));
      fnv1a(file_mtime_ns(d, ec));
      fnv1a(file_size_u64(d, ec));
    }

    return h;
  }

  /**
   * @brief List depfiles generated by CMake for a target.
   */
  inline std::vector<fs::path> list_depfiles_for_target(
      const fs::path &buildDir,
      const std::string &targetName)
  {
    std::vector<fs::path> out;
    std::error_code ec;

    const fs::path dir = buildDir / "CMakeFiles" / (targetName + ".dir");
    if (!fs::exists(dir, ec) || ec)
      return out;

    for (auto it = fs::recursive_directory_iterator(
             dir,
             fs::directory_options::skip_permission_denied,
             ec);
         !ec && it != fs::recursive_directory_iterator();
         ++it)
    {
      if (!it->is_regular_file())
        continue;

      const auto p = it->path();
      if (p.extension() == ".d")
        out.push_back(p);
    }

    std::sort(out.begin(), out.end(),
              [](const fs::path &a, const fs::path &b)
              {
                return a.string() < b.string();
              });

    return out;
  }

  /**
   * @brief Parse dependency paths from a Make/Ninja depfile content buffer.
   */
  inline void depfile_parse_paths(const std::string &content, std::vector<fs::path> &paths)
  {
    const auto pos = content.find(':');
    if (pos == std::string::npos)
      return;

    std::string deps = content.substr(pos + 1);

    std::vector<std::string> toks;
    toks.reserve(128);

    std::string cur;
    cur.reserve(128);

    auto flush = [&]()
    {
      if (!cur.empty())
      {
        toks.push_back(cur);
        cur.clear();
      }
    };

    for (std::size_t i = 0; i < deps.size(); ++i)
    {
      const char c = deps[i];

      if (c == '\\')
      {
        if (i + 1 < deps.size())
        {
          const char n = deps[i + 1];

          if (n == '\n' || n == '\r')
          {
            ++i;
            if (i + 1 < deps.size() && deps[i + 1] == '\n')
              ++i;
            continue;
          }

          cur.push_back(n);
          ++i;
          continue;
        }

        flush();
        continue;
      }

      if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
      {
        flush();
        continue;
      }

      cur.push_back(c);
    }

    flush();

    for (const auto &t : toks)
    {
      if (!t.empty())
        paths.push_back(fs::path(t));
    }
  }

  /**
   * @brief Normalize a relative dep path against the build directory when possible.
   */
  inline fs::path normalize_dep_path(const fs::path &buildDir, const fs::path &p)
  {
    if (p.empty())
      return p;

    if (p.is_absolute())
      return p;

    std::error_code ec;
    fs::path cand = buildDir / p;
    if (fs::exists(cand, ec) && !ec)
      return cand;

    return p;
  }

  /**
   * @brief Compute the maximum dependency mtime referenced by a list of depfiles.
   */
  inline std::optional<std::uint64_t> compute_max_dep_mtime_ns(
      const fs::path &buildDir,
      const std::vector<fs::path> &depfiles)
  {
    std::uint64_t maxNs = 0;
    std::vector<fs::path> deps;
    deps.reserve(512);

    for (const auto &d : depfiles)
    {
      std::ifstream ifs(d);
      if (!ifs)
        return std::nullopt;

      std::ostringstream ss;
      ss << ifs.rdbuf();

      deps.clear();
      depfile_parse_paths(ss.str(), deps);

      for (auto &p : deps)
      {
        const fs::path dep = normalize_dep_path(buildDir, p);

        std::error_code ec;
        if (!fs::exists(dep, ec) || ec)
          continue;

        const auto t = file_mtime_ns(dep, ec);
        if (!ec && t > maxNs)
          maxNs = t;
      }
    }

    return maxNs;
  }

  /**
   * @brief Return whether the executable must be rebuilt according to depfiles and cache.
   */
  inline bool needs_rebuild_from_depfiles_cached(
      const fs::path &exePath,
      const fs::path &buildDir,
      const std::string &targetName)
  {
    std::error_code ec;

    if (!fs::exists(exePath, ec) || ec)
      return true;

    const auto depfiles = list_depfiles_for_target(buildDir, targetName);
    if (depfiles.empty())
      return true;

    const fs::path stampFile =
        buildDir / (".vix-rebuild-cache-" + targetName + ".txt");

    const std::uint64_t exeMtime = file_mtime_ns(exePath, ec);
    if (ec || exeMtime == 0)
      return true;

    const std::uint64_t fpNow = depfiles_fingerprint_fast(depfiles);

    if (auto st = load_rebuild_cache_stamp(stampFile))
    {
      if (st->depfiles_fingerprint == fpNow)
      {
        if (exeMtime >= st->max_dep_mtime_ns)
          return false;
      }
    }

    auto maxDep = compute_max_dep_mtime_ns(buildDir, depfiles);
    if (!maxDep)
      return true;

    RebuildCacheStamp out{};
    out.exe_mtime_ns = exeMtime;
    out.depfiles_fingerprint = fpNow;
    out.max_dep_mtime_ns = *maxDep;

    save_rebuild_cache_stamp(stampFile, out);

    return exeMtime < *maxDep;
  }

} // namespace vix::commands::RunCommand::detail

#endif
