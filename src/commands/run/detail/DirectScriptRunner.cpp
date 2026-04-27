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
      for (unsigned char c : input)
      {
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
     * @brief Serialize a direct script cache metadata buffer.
     */
    std::string make_direct_cache_meta(
        const fs::path &scriptPath,
        const DirectScriptPlan &plan)
    {
      std::ostringstream oss;

      oss << "script=" << scriptPath.string() << "\n";
      oss << "script_mtime_ns=" << file_mtime_ns_local(scriptPath) << "\n";
      oss << "script_content_hash=" << file_content_hash_hex(scriptPath) << "\n";
      oss << "cache_key=" << plan.cacheKey << "\n";
      oss << "compile_cmd=" << plan.compileCmd << "\n";
      oss << "run_cmd=" << plan.runCmd << "\n";

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

        if (const auto lib = find_vix_lib())
        {
          cmd << " " << process::quote(lib->string());
        }
        else
        {
          const auto libs = find_vix_all_module_libs();
          if (!libs.empty())
          {
#ifndef __APPLE__
            cmd << " -Wl,--start-group";
#endif
            for (const auto &lib : libs)
              cmd << " " << process::quote(lib.string());
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
    std::ostringstream sig;

    const fs::path abs = fs::absolute(cppPath).lexically_normal();

    constexpr const char *kDirectRunnerCacheRev = "2";

    sig << "direct_runner_rev=" << kDirectRunnerCacheRev << ";";
    sig << "script=" << abs.string() << ";";
    sig << "mtime=" << file_mtime_ns_local(abs) << ";";
    sig << "content=" << file_content_hash_hex(abs) << ";";
    sig << "direct=1;";
    sig << "san=" << text::bool01(want_sanitizers(opt.enableSanitizers, opt.enableUbsanOnly)) << ";";
    sig << "san_mode=" << sanitizer_mode_string(opt.enableSanitizers, opt.enableUbsanOnly) << ";";
    sig << "sqlite=" << text::bool01(opt.withSqlite) << ";";
    sig << "mysql=" << text::bool01(opt.withMySql) << ";";

    for (const auto &v : probe.includeDirs)
      sig << "I=" << v << ";";

    for (const auto &v : probe.systemIncludeDirs)
      sig << "IS=" << v << ";";

    for (const auto &v : probe.defines)
      sig << "D=" << v << ";";

    for (const auto &v : probe.compileOpts)
      sig << "C=" << v << ";";

    for (const auto &v : probe.libDirs)
      sig << "L=" << v << ";";

    for (const auto &v : probe.libs)
      sig << "l=" << v << ";";

    for (const auto &v : probe.linkOpts)
      sig << "W=" << v << ";";

    return hex_u64(fnv1a_64(sig.str()));
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
    plan.cacheKey = make_direct_script_cache_key(plan.scriptPath, probe, opt);
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
      info("Direct compile: " + plan.scriptPath.filename().string());

      const LiveRunResult build = run_cmd_live_filtered_capture(
          plan.compileCmd,
          "Compiling script...",
          false,
          0,
          opt.enableSanitizers || opt.enableUbsanOnly,
          true);

      if (build.exitCode != 0)
      {
        std::cerr << "[DEBUG NEW VIX] direct compile failure path reached\n";
        std::cerr << "[DEBUG NEW VIX] exitCode=" << build.exitCode << "\n";
        std::cerr << "[DEBUG NEW VIX] stdout empty=" << std::boolalpha << build.stdoutText.empty() << "\n";
        std::cerr << "[DEBUG NEW VIX] stderr empty=" << std::boolalpha << build.stderrText.empty() << "\n";

        bool handled = false;

        if (!build.stdoutText.empty() || !build.stderrText.empty())
        {
          const std::string compileLog = build.stdoutText + build.stderrText;

          std::cerr << "[DEBUG NEW VIX] compileLog size=" << compileLog.size() << "\n";
          std::cerr << "[DEBUG NEW VIX] calling printBuildErrors(...)\n";

          handled = vix::cli::ErrorHandler::printBuildErrors(
              compileLog,
              plan.scriptPath,
              "Script compile failed");

          std::cerr << "[DEBUG NEW VIX] printBuildErrors handled=" << handled << "\n";
        }

        if (!handled)
        {
          std::cerr << "[DEBUG NEW VIX] fallback: error(\"Script compile failed.\")\n";
          error("Script compile failed.");
        }

        return build.exitCode != 0 ? build.exitCode : 1;
      }

      const std::string meta = make_direct_cache_meta(plan.scriptPath, plan);
      text::write_text_file(cache.metaFile, meta);
    }
    else
    {
      success("Using cached direct build");
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

    handle_runtime_exit_code(run.exitCode, "run", run.failureHandled);
    return run.exitCode;
  }

} // namespace vix::commands::RunCommand::detail
