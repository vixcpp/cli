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

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <system_error>
#include <fstream>
#include <sstream>
#include <algorithm>

#include <vix/cli/ErrorHandler.hpp>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace vix::commands::RunCommand::detail
{
  namespace fs = std::filesystem;

  struct Options
  {
    std::string appName;
    std::string preset = "dev-ninja";
    std::string runPreset;
    std::string dir;
    int jobs = 0;

    bool quiet = false;
    bool verbose = false;
    std::string logLevel;
    std::string logFormat;
    std::string logColor; // --log-color (auto|always|never)
    bool noColor = false; // --no-color

    std::string exampleName;

    // Single .cpp mode
    bool singleCpp = false;
    std::filesystem::path cppFile;
    bool watch = false;

    bool forceServerLike = false;  // --force-server
    bool forceScriptLike = false;  // --force-script
    bool enableSanitizers = false; // --san  (ASan+UBSan)
    bool enableUbsanOnly = false;  // --ubsan (UBSan only)

    std::string clearMode = "auto";
    std::vector<std::string> scriptFlags;

    // .vix manifest mode (vix run app.vix)
    bool manifestMode = false;
    std::filesystem::path manifestFile;

    // Run extras from manifest (V1)
    std::vector<std::string> runArgs; // [run] args = ["--port","8080"]
    std::vector<std::string> runEnv;  // [run] env  = ["K=V","X=1"]
    int timeoutSec = 0;               // [run] timeout_sec = 15
    std::string cwd;
    bool badDoubleDashRuntimeArgs = false;
    std::string badDoubleDashArg;
  };

  // Process / IO
  int run_cmd_live_filtered(
      const std::string &cmd,
      const std::string &spinnerLabel = {});

  std::filesystem::path manifest_entry_cpp(const std::filesystem::path &manifestFile);

  inline int normalize_exit_code(int code) noexcept
  {
#ifdef _WIN32
    return code;
#else
    if (code < 0)
      return 1;

    if (WIFEXITED(code))
      return WEXITSTATUS(code);

    if (WIFSIGNALED(code))
      return 128 + WTERMSIG(code);

    return 1;
#endif
  }

#ifndef _WIN32
  struct LiveRunResult
  {
    int rawStatus = 0; // waitpid status
    int exitCode = 0;  // normalized 0..255 or 128+signal
    std::string stdoutText;
    std::string stderrText;
    bool failureHandled = false;
    bool printed_live = false;
  };

  LiveRunResult run_cmd_live_filtered_capture(
      const std::string &cmd,
      const std::string &spinnerLabel,
      bool passthroughRuntime,
      int timeoutSec = 0);
#endif

  // Script mode (vix run foo.cpp)
  std::filesystem::path get_scripts_root();

  /// Detect whether a .cpp script depends on Vix runtime
  bool script_uses_vix(const std::filesystem::path &cppPath);

  std::string make_script_cmakelists(
      const std::string &exeName,
      const fs::path &cppPath,
      bool useVixRuntime,
      const std::vector<std::string> &scriptFlags);

  int run_single_cpp(const Options &opt);
  int run_single_cpp_watch(const Options &opt);
  int run_project_watch(const Options &opt, const fs::path &projectDir);

  // CLI parsing
  Options parse(const std::vector<std::string> &args);

  // Build / run flow helpers
  std::string quote(const std::string &s);
  void handle_runtime_exit_code(
      int code,
      const std::string &context,
      bool alreadyHandled);

  bool has_presets(const fs::path &projectDir);
  std::string choose_run_preset(
      const fs::path &dir,
      const std::string &configurePreset,
      const std::string &userRunPreset);

  bool has_cmake_cache(const fs::path &buildDir);

  std::optional<fs::path> choose_project_dir(
      const Options &opt,
      const fs::path &cwd);

  void apply_log_level_env(const Options &opt);

  std::optional<fs::path> preset_binary_dir(
      const fs::path &projectDir,
      const std::string &configurePreset);

  // Execution helpers (capturing output)
  std::string run_and_capture_with_code(const std::string &cmd, int &exitCode);
  std::string run_and_capture(const std::string &cmd);

  // Build log analysis
  bool has_real_build_work(const std::string &log);

  void apply_log_format_env(const Options &opt);
  void apply_log_color_env(const Options &opt);

  std::string join_quoted_args_local(const std::vector<std::string> &a);
  std::string wrap_with_cwd_if_needed(const Options &opt, const std::string &cmd);

  inline void apply_log_env(const Options &opt)
  {
    apply_log_level_env(opt);
    apply_log_format_env(opt);
    apply_log_color_env(opt);
  }

  std::string choose_configure_preset_smart(
      const std::filesystem::path &projectDir,
      const std::string &userPreset);

  std::filesystem::path resolve_build_dir_smart(
      const std::filesystem::path &projectDir,
      const std::string &configurePreset);

  inline int effective_timeout_sec(const Options &opt)
  {
    if (opt.forceServerLike || opt.watch)
      return 0;

    return opt.timeoutSec;
  }

  inline std::string normalize_cwd_if_needed(const std::string &cwd)
  {
    if (cwd.empty())
      return {};

    std::error_code ec{};
    fs::path p(cwd);

    if (p.is_relative())
      p = fs::absolute(p, ec);

    if (ec)
      return cwd; // fallback

    return p.string();
  }

  struct RebuildCacheStamp
  {
    std::uint64_t exe_mtime_ns = 0;
    std::uint64_t depfiles_fingerprint = 0; // changes if any .d changes
    std::uint64_t max_dep_mtime_ns = 0;     // max mtime among resolved deps
  };

  inline std::uint64_t file_mtime_ns(const fs::path &p, std::error_code &ec)
  {
    ec.clear();
    const auto ft = fs::last_write_time(p, ec);
    if (ec)
      return 0;

    // Convert to nanoseconds in a portable-ish way
    using namespace std::chrono;
    const auto ns = duration_cast<nanoseconds>(ft.time_since_epoch()).count();
    return (ns < 0) ? 0ull : static_cast<std::uint64_t>(ns);
  }

  inline std::uint64_t file_size_u64(const fs::path &p, std::error_code &ec)
  {
    ec.clear();
    const auto sz = fs::file_size(p, ec);
    if (ec)
      return 0;
    return static_cast<std::uint64_t>(sz);
  }

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

    if (s.depfiles_fingerprint == 0 && s.max_dep_mtime_ns == 0 && s.exe_mtime_ns == 0)
      return std::nullopt;

    return s;
  }

  inline void save_rebuild_cache_stamp(const fs::path &stampFile, const RebuildCacheStamp &s)
  {
    std::ofstream ofs(stampFile, std::ios::trunc);
    if (!ofs)
      return;

    ofs << "exe_mtime_ns=" << s.exe_mtime_ns << "\n";
    ofs << "depfiles_fingerprint=" << s.depfiles_fingerprint << "\n";
    ofs << "max_dep_mtime_ns=" << s.max_dep_mtime_ns << "\n";
  }

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

      // mtime + size
      fnv1a(file_mtime_ns(d, ec));
      fnv1a(file_size_u64(d, ec));
    }
    return h;
  }

  // CMake usually: buildDir/CMakeFiles/<target>.dir/*.d
  inline std::vector<fs::path> list_depfiles_for_target(
      const fs::path &buildDir,
      const std::string &targetName)
  {
    std::vector<fs::path> out;
    std::error_code ec;

    fs::path dir = buildDir / "CMakeFiles" / (targetName + ".dir");
    if (!fs::exists(dir, ec) || ec)
      return out;

    for (auto it = fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator();
         ++it)
    {
      if (!it->is_regular_file())
        continue;
      const auto p = it->path();
      if (p.extension() == ".d")
        out.push_back(p);
    }

    std::sort(out.begin(), out.end(), [](const fs::path &a, const fs::path &b)
              { return a.string() < b.string(); });

    return out;
  }

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
      if (t.empty())
        continue;
      paths.push_back(fs::path(t));
    }
  }

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

  inline std::optional<std::uint64_t> compute_max_dep_mtime_ns(
      const fs::path &buildDir,
      const std::vector<fs::path> &depfiles)
  {
    std::error_code ec;
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

        std::error_code e2;
        if (!fs::exists(dep, e2) || e2)
        {
          continue;
        }

        const auto t = file_mtime_ns(dep, e2);
        if (!e2 && t > maxNs)
          maxNs = t;
      }
    }

    return maxNs;
  }

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

    if (exeMtime < *maxDep)
      return true;

    return false;
  }

} // namespace vix::commands::RunCommand::detail

#endif
