/**
 *
 *  @file DirectScriptRunner.cpp
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
#include <vix/cli/commands/run/detail/DirectScriptRunner.hpp>
#include <vix/cli/commands/helpers/ProcessHelpers.hpp>
#include <vix/cli/commands/helpers/TextHelpers.hpp>
#include <vix/cli/commands/run/RunScriptHelpers.hpp>
#include <vix/cli/ErrorHandler.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <signal.h>
#endif

namespace vix::commands::RunCommand::detail
{
  namespace process = vix::cli::commands::helpers;
  namespace text = vix::cli::commands::helpers;

  using namespace vix::cli::style;

  namespace
  {
    /**
     * @brief Return the preferred compiler executable for direct script mode.
     */
    std::string choose_cxx_compiler()
    {
      if (const char *env = vix::utils::vix_getenv("CXX"); env && *env)
        return std::string(env);

#ifdef _WIN32
      return "g++";
#else
      return "c++";
#endif
    }

    /**
     * @brief Return the executable suffix for the current platform.
     */
    std::string executable_suffix()
    {
#ifdef _WIN32
      return ".exe";
#else
      return "";
#endif
    }

#ifndef _WIN32
    bool is_user_interrupt_result(const LiveRunResult &result) noexcept
    {
      return result.exitCode == 130 ||
             (result.terminatedBySignal && result.termSignal == SIGINT);
    }
#else
    bool is_user_interrupt_result(const LiveRunResult &result) noexcept
    {
      return result.exitCode == 130;
    }
#endif

    /**
     * @brief Return a filesystem-safe executable name.
     */
    std::string sanitize_exe_name(std::string s)
    {
      if (s.empty())
        return "script";

      for (char &c : s)
      {
        const unsigned char uc = static_cast<unsigned char>(c);
        const bool ok =
            (uc >= 'a' && uc <= 'z') ||
            (uc >= 'A' && uc <= 'Z') ||
            (uc >= '0' && uc <= '9') ||
            c == '_' || c == '-';

        if (!ok)
          c = '_';
      }

      return s;
    }

    /**
     * @brief Read a file into memory, or return an empty string on failure.
     */
    std::string read_file_or_empty(const fs::path &p)
    {
      std::ifstream ifs(p, std::ios::binary);
      if (!ifs)
        return {};

      std::ostringstream oss;
      oss << ifs.rdbuf();
      return oss.str();
    }

    /**
     * @brief 64-bit FNV-1a hash.
     */
    std::uint64_t fnv1a_64(const std::string &input)
    {
      constexpr std::uint64_t offset = 14695981039346656037ull;
      constexpr std::uint64_t prime = 1099511628211ull;

      std::uint64_t h = offset;
      for (char ch : input)
      {
        const auto c = static_cast<unsigned char>(ch);
        h ^= static_cast<std::uint64_t>(c);
        h *= prime;
      }

      return h;
    }

    /**
     * @brief Convert a u64 value to fixed lowercase hex.
     */
    std::string hex_u64(std::uint64_t value)
    {
      static constexpr char digits[] = "0123456789abcdef";
      std::string out(16, '0');

      for (int i = 15; i >= 0; --i)
      {
        out[static_cast<std::size_t>(i)] = digits[value & 0xF];
        value >>= 4u;
      }

      return out;
    }

    /**
     * @brief Compute a stable content hash for the script source.
     */
    std::string file_content_hash_hex(const fs::path &p)
    {
      return hex_u64(fnv1a_64(read_file_or_empty(p)));
    }

    /**
     * @brief Append a shell-quoted value to a command stream.
     */
    void append_quoted(std::ostringstream &cmd, const std::string &value)
    {
      if (value.empty())
        return;

      cmd << " " << process::quote(value);
    }

    /**
     * @brief Return true when the file exists.
     */
    bool file_exists(const fs::path &p)
    {
      std::error_code ec;
      return fs::exists(p, ec) && !ec;
    }

    /**
     * @brief Return the file mtime in nanoseconds, or 0 on failure.
     */
    std::uint64_t file_mtime_ns_local(const fs::path &p)
    {
      std::error_code ec;
      return file_mtime_ns(p, ec);
    }

    /**
     * @brief Return the current Vix version string used by cache fingerprints.
     */
    std::string vix_version_string()
    {
      if (const char *v = vix::utils::vix_getenv("VIX_VERSION"); v && *v)
        return std::string(v);

      return "unknown";
    }

    /**
     * @brief Run a compiler query and return a trimmed result.
     */
    std::string compiler_query(const std::string &compiler, const std::string &arg)
    {
      int code = 0;
      const std::string out = run_and_capture_with_code(
          process::quote(compiler) + " " + arg,
          code);

      if (code != 0)
        return "unknown";

      std::string s = out;
      while (!s.empty() &&
             (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
      {
        s.pop_back();
      }

      return s.empty() ? "unknown" : s;
    }

    /**
     * @brief Return the compiler version used by direct script mode.
     */
    std::string compiler_version_string(const std::string &compiler)
    {
#ifdef _WIN32
      return "unknown";
#else
      return compiler_query(compiler, "-dumpfullversion -dumpversion");
#endif
    }

    /**
     * @brief Return the compiler target triple used by direct script mode.
     */
    std::string compiler_target_triple(const std::string &compiler)
    {
#ifdef _WIN32
      return "windows";
#else
      return compiler_query(compiler, "-dumpmachine");
#endif
    }

    /**
     * @brief Detect the effective C++ standard used by direct script mode.
     */
    std::string detect_cpp_standard(const ScriptProbeResult &probe)
    {
      for (const auto &opt : probe.compileOpts)
      {
        if (opt.rfind("-std=", 0) == 0)
          return opt.substr(5);
      }

      return "c++20";
    }

    /**
     * @brief Return the logical build mode for direct script mode.
     */
    std::string direct_build_mode_string(const Options &opt)
    {
      std::ostringstream oss;

      oss << "direct";
      oss << ";san=" << text::bool01(want_sanitizers(opt.enableSanitizers, opt.enableUbsanOnly));
      oss << ";san_mode=" << sanitizer_mode_string(opt.enableSanitizers, opt.enableUbsanOnly);
      oss << ";sqlite=" << text::bool01(opt.withSqlite);
      oss << ";mysql=" << text::bool01(opt.withMySql);

      return oss.str();
    }

    /**
     * @brief Add a stable vector block to a fingerprint stream.
     */
    void append_fingerprint_list(
        std::ostringstream &oss,
        const char *name,
        const std::vector<std::string> &values)
    {
      oss << name << ".count=" << values.size() << "\n";

      for (const auto &value : values)
        oss << name << "[]=" << value << "\n";
    }

    /**
     * @brief Create a cheap fingerprint for one filesystem path.
     */
    std::string path_fingerprint(const fs::path &p)
    {
      std::error_code ec;
      const fs::path abs = fs::absolute(p, ec).lexically_normal();

      const auto mtime = file_mtime_ns_local(abs);

      ec.clear();
      const auto size = fs::is_regular_file(abs, ec) && !ec
                            ? file_size_u64(abs, ec)
                            : 0ull;

      std::ostringstream oss;
      oss << abs.string() << "|mtime=" << mtime << "|size=" << size;
      return oss.str();
    }

    /**
     * @brief Collect fingerprints for known dependency paths.
     */
    std::vector<std::string> collect_dep_fingerprints(const ScriptProbeResult &probe)
    {
      std::vector<std::string> out;

      for (const auto &p : probe.compiledDepPaths)
        out.push_back(path_fingerprint(p));

      std::sort(out.begin(), out.end());
      return out;
    }

    /**
     * @brief Collect header fingerprints from header-only dependency include roots.
     */
    std::vector<std::string> collect_header_fingerprints(const ScriptProbeResult &probe)
    {
      std::vector<std::string> out;

      for (const auto &root : probe.headerOnlyDepIncludeDirs)
      {
        std::error_code ec;
        if (!fs::exists(root, ec) || ec)
          continue;

        for (auto it = fs::recursive_directory_iterator(
                 root,
                 fs::directory_options::skip_permission_denied,
                 ec);
             !ec && it != fs::recursive_directory_iterator();
             ++it)
        {
          if (!it->is_regular_file())
            continue;

          const auto ext = it->path().extension().string();
          if (ext == ".h" ||
              ext == ".hpp" ||
              ext == ".hh" ||
              ext == ".hxx" ||
              ext == ".ipp")
          {
            out.push_back(path_fingerprint(it->path()));
          }
        }
      }

      std::sort(out.begin(), out.end());
      return out;
    }

    /**
     * @brief Build the full deterministic fingerprint for a direct script build.
     */
    DirectBuildFingerprint make_direct_build_fingerprint(
        const fs::path &cppPath,
        const ScriptProbeResult &probe,
        const Options &opt)
    {
      const fs::path abs = fs::absolute(cppPath).lexically_normal();
      const std::string compiler = choose_cxx_compiler();

      DirectBuildFingerprint fp{};
      fp.formatVersion = "1";
      fp.vixVersion = vix_version_string();

      fp.compilerPath = compiler;
      fp.compilerVersion = compiler_version_string(compiler);
      fp.targetTriple = compiler_target_triple(compiler);

      fp.cppStandard = detect_cpp_standard(probe);
      fp.buildMode = direct_build_mode_string(opt);

      fp.scriptPath = abs.string();
      fp.scriptContentHash = file_content_hash_hex(abs);
      fp.scriptMtimeNs = file_mtime_ns_local(abs);

      fp.includeDirs = probe.includeDirs;
      fp.systemIncludeDirs = probe.systemIncludeDirs;
      fp.defines = probe.defines;
      fp.compileOpts = probe.compileOpts;

      fp.libDirs = probe.libDirs;
      fp.libs = probe.libs;
      fp.linkOpts = probe.linkOpts;

      fp.depFingerprints = collect_dep_fingerprints(probe);
      fp.headerFingerprints = collect_header_fingerprints(probe);

      return fp;
    }

    /**
     * @brief Serialize a direct build fingerprint in a stable text format.
     */
    std::string serialize_direct_build_fingerprint(const DirectBuildFingerprint &fp)
    {
      std::ostringstream oss;

      oss << "format_version=" << fp.formatVersion << "\n";
      oss << "vix_version=" << fp.vixVersion << "\n";

      oss << "compiler_path=" << fp.compilerPath << "\n";
      oss << "compiler_version=" << fp.compilerVersion << "\n";
      oss << "target_triple=" << fp.targetTriple << "\n";

      oss << "cpp_standard=" << fp.cppStandard << "\n";
      oss << "build_mode=" << fp.buildMode << "\n";

      oss << "script_path=" << fp.scriptPath << "\n";
      oss << "script_content_hash=" << fp.scriptContentHash << "\n";
      oss << "script_mtime_ns=" << fp.scriptMtimeNs << "\n";

      append_fingerprint_list(oss, "include_dirs", fp.includeDirs);
      append_fingerprint_list(oss, "system_include_dirs", fp.systemIncludeDirs);
      append_fingerprint_list(oss, "defines", fp.defines);
      append_fingerprint_list(oss, "compile_opts", fp.compileOpts);

      append_fingerprint_list(oss, "lib_dirs", fp.libDirs);
      append_fingerprint_list(oss, "libs", fp.libs);
      append_fingerprint_list(oss, "link_opts", fp.linkOpts);

      append_fingerprint_list(oss, "dep_fingerprints", fp.depFingerprints);
      append_fingerprint_list(oss, "header_fingerprints", fp.headerFingerprints);

      return oss.str();
    }

    /**
     * @brief Return the cache key derived from a direct build fingerprint.
     */
    std::string direct_build_fingerprint_cache_key(const DirectBuildFingerprint &fp)
    {
      return hex_u64(fnv1a_64(serialize_direct_build_fingerprint(fp)));
    }

    /**
     * @brief Serialize a direct script cache metadata buffer.
     */
    std::string make_direct_cache_meta(
        const fs::path &scriptPath,
        const DirectScriptPlan &plan)
    {
      std::ostringstream oss;

      oss << "script=" << scriptPath.string() << "\n";
      oss << "script_mtime_ns=" << plan.fingerprint.scriptMtimeNs << "\n";
      oss << "script_content_hash=" << plan.fingerprint.scriptContentHash << "\n";
      oss << "cache_key=" << plan.cacheKey << "\n";

      oss << "vix_version=" << plan.fingerprint.vixVersion << "\n";
      oss << "compiler_path=" << plan.fingerprint.compilerPath << "\n";
      oss << "compiler_version=" << plan.fingerprint.compilerVersion << "\n";
      oss << "target_triple=" << plan.fingerprint.targetTriple << "\n";
      oss << "cpp_standard=" << plan.fingerprint.cppStandard << "\n";
      oss << "build_mode=" << plan.fingerprint.buildMode << "\n";

      oss << "compile_cmd=" << plan.compileCmd << "\n";
      oss << "run_cmd=" << plan.runCmd << "\n";

      oss << "\n[fingerprint]\n";
      oss << serialize_direct_build_fingerprint(plan.fingerprint);

      return oss.str();
    }

    /**
     * @brief Return whether the direct cache metadata matches the current script content.
     */
    bool direct_cache_is_valid(const DirectScriptPlan &plan, const DirectScriptCacheState &cache)
    {
      if (!file_exists(plan.scriptPath))
        return false;

      if (!file_exists(plan.binaryPath))
        return false;

      if (!file_exists(cache.metaFile))
        return false;

      const std::string meta = text::read_text_file_or_empty(cache.metaFile);
      if (meta.empty())
        return false;

      const std::string wantKey = "cache_key=" + plan.cacheKey + "\n";
      if (meta.find(wantKey) == std::string::npos)
        return false;

      const std::string wantMtime =
          "script_mtime_ns=" + std::to_string(file_mtime_ns_local(plan.scriptPath)) + "\n";
      if (meta.find(wantMtime) == std::string::npos)
        return false;

      const std::string wantContentHash =
          "script_content_hash=" + file_content_hash_hex(plan.scriptPath) + "\n";
      if (meta.find(wantContentHash) == std::string::npos)
        return false;

      return true;
    }

    /**
     * @brief Build the compile command for the direct path.
     */
    std::string make_direct_compile_cmd(const Options &opt, const DirectScriptPlan &plan)
    {
      std::ostringstream cmd;

      cmd << choose_cxx_compiler();

      append_quoted(cmd, plan.scriptPath.string());
      cmd << " -o";
      append_quoted(cmd, plan.binaryPath.string());

      bool hasStd = false;
      for (const auto &compileOpt : plan.probe.compileOpts)
      {
        if (compileOpt.rfind("-std=", 0) == 0)
        {
          hasStd = true;
          break;
        }
      }

      if (!hasStd)
        cmd << " -std=c++20";

      // --- Vix runtime fast path ---
      // If vix is installed (~/.vix/include + ~/.vix/lib/libvix.a), link directly
      // without going through CMake. If a PCH exists, use it to skip reparsing
      // all Vix headers on every compile.
      if (plan.probe.usesVixRuntime)
      {
        if (const auto incDir = find_vix_include_dir())
          cmd << " -I" << process::quote(incDir->string());

        if (const auto pch = find_vix_pch())
          cmd << " -include-pch " << process::quote(pch->string());

        if (const auto vixLib = find_vix_lib())
        {
          cmd << " " << process::quote(vixLib->string());
        }
        else
        {
          const auto libs = find_vix_all_module_libs();
          if (!libs.empty())
          {
#ifndef __APPLE__
            cmd << " -Wl,--start-group";
#endif
            for (const auto &moduleLib : libs)
              cmd << " " << process::quote(moduleLib.string());
#ifndef __APPLE__
            cmd << " -Wl,--end-group";
#endif
          }
        }

        cmd << " -lpthread -ldl";
#ifdef __APPLE__
        cmd << " -framework CoreFoundation";
#endif
      }

      for (const auto &inc : plan.probe.includeDirs)
        cmd << " -I" << process::quote(inc);

      for (const auto &inc : plan.probe.systemIncludeDirs)
        cmd << " -isystem " << process::quote(inc);

      for (const auto &def : plan.probe.defines)
        cmd << " -D" << def;

      for (const auto &compileOpt : plan.probe.compileOpts)
        append_quoted(cmd, compileOpt);

      const bool san = want_sanitizers(opt.enableSanitizers, opt.enableUbsanOnly);
      if (san)
      {
        if (opt.enableUbsanOnly)
        {
          cmd << " -fsanitize=undefined";
        }
        else
        {
          cmd << " -fsanitize=address,undefined";
          cmd << " -fno-omit-frame-pointer";
        }

        cmd << " -g";
      }

      for (const auto &dir : plan.probe.libDirs)
        cmd << " -L" << process::quote(dir);

      for (const auto &lib : plan.probe.libs)
        cmd << " -l" << lib;

      for (const auto &linkOpt : plan.probe.linkOpts)
        append_quoted(cmd, linkOpt);

      return cmd.str();
    }

    /**
     * @brief Build the runtime command for the direct path.
     */
    std::string make_direct_run_cmd(const Options &opt, const DirectScriptPlan &plan)
    {
      std::string cmd = process::quote(plan.binaryPath.string());
      cmd += join_quoted_args_local(opt.runArgs);
      return wrap_with_cwd_if_needed(opt, cmd);
    }

  } // namespace

  fs::path get_direct_scripts_cache_root()
  {
#ifdef _WIN32
    const char *home = vix::utils::vix_getenv("USERPROFILE");
#else
    const char *home = vix::utils::vix_getenv("HOME");
#endif

    if (home && *home)
      return fs::path(home) / ".vix" / "cache" / "scripts";

    return fs::path(".vix") / "cache" / "scripts";
  }

  std::string make_direct_script_cache_key(
      const fs::path &cppPath,
      const ScriptProbeResult &probe,
      const Options &opt)
  {
    const DirectBuildFingerprint fp =
        make_direct_build_fingerprint(cppPath, probe, opt);

    return direct_build_fingerprint_cache_key(fp);
  }

  DirectScriptCacheState load_direct_script_cache_state(const DirectScriptPlan &plan)
  {
    DirectScriptCacheState out{};
    out.rootDir = plan.cacheDir;
    out.binaryPath = plan.binaryPath;
    out.metaFile = plan.cacheDir / "meta.txt";
    out.stdoutLogPath = plan.cacheDir / "stdout.log";
    out.stderrLogPath = plan.cacheDir / "stderr.log";
    out.cacheKey = plan.cacheKey;
    out.cacheHit = false;
    out.needsRebuild = true;

    if (direct_cache_is_valid(plan, out))
    {
      out.cacheHit = true;
      out.needsRebuild = false;
    }

    return out;
  }

  DirectScriptPlan make_direct_script_plan(
      const Options &opt,
      const ScriptProbeResult &probe)
  {
    DirectScriptPlan plan{};
    plan.scriptPath = fs::absolute(opt.cppFile).lexically_normal();
    plan.workingDir = plan.scriptPath.parent_path();

    const std::string stem = sanitize_exe_name(plan.scriptPath.stem().string());

    plan.exeName = stem.empty() ? "script" : stem;

    plan.fingerprint = make_direct_build_fingerprint(plan.scriptPath, probe, opt);
    plan.cacheKey = direct_build_fingerprint_cache_key(plan.fingerprint);
    plan.cacheDir = get_direct_scripts_cache_root() / plan.cacheKey;

    plan.binaryPath = plan.cacheDir / (plan.exeName + executable_suffix());

    plan.shouldRun = true;
    plan.passthroughRuntime = !opt.forceServerLike;
    plan.effectiveTimeoutSec = effective_timeout_sec(opt);
    plan.probe = probe;

    const auto cache = load_direct_script_cache_state(plan);
    plan.shouldCompile = cache.needsRebuild;

    plan.compileCmd = make_direct_compile_cmd(opt, plan);
    plan.runCmd = make_direct_run_cmd(opt, plan);

    return plan;
  }

  int run_single_cpp_direct(const Options &opt, const DirectScriptPlan &plan)
  {
    std::error_code ec;
    fs::create_directories(plan.cacheDir, ec);

    if (ec)
    {
      error("Failed to create direct script cache directory.");
      return 1;
    }

#ifndef _WIN32
    apply_sanitizer_env_if_needed(opt.enableSanitizers, opt.enableUbsanOnly);
#endif

    const auto cache = load_direct_script_cache_state(plan);

    if (plan.shouldCompile)
    {
      const LiveRunResult build = run_cmd_live_filtered_capture(
          plan.compileCmd,
          "",
          false,
          0,
          opt.enableSanitizers || opt.enableUbsanOnly,
          true);

      if (build.exitCode != 0)
      {
        bool handled = false;

        if (!build.stdoutText.empty() || !build.stderrText.empty())
        {
          const std::string compileLog = build.stdoutText + build.stderrText;

          handled = vix::cli::ErrorHandler::printBuildErrors(
              compileLog,
              plan.scriptPath,
              "Script compile failed");
        }

        if (!handled)
        {
          error("Script compile failed.");
        }

        return build.exitCode != 0 ? build.exitCode : 1;
      }

      const std::string meta = make_direct_cache_meta(plan.scriptPath, plan);
      text::write_text_file(cache.metaFile, meta);
    }

    if (!plan.shouldRun)
      return 0;

    const LiveRunResult run = run_cmd_live_filtered_capture(
        plan.runCmd,
        "Running script...",
        plan.passthroughRuntime,
        plan.effectiveTimeoutSec,
        opt.enableSanitizers || opt.enableUbsanOnly,
        false);

    if (is_user_interrupt_result(run))
    {
      hint("ℹ Program interrupted by user (SIGINT).");
      return 0;
    }

    handle_runtime_exit_code(run.exitCode, "run", run.failureHandled);
    return run.exitCode;
  }

} // namespace vix::commands::RunCommand::detail
