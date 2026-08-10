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
#include <vix/cli/commands/replay/ReplayCapture.hpp>
#include <vix/cli/commands/replay/ReplayRecorder.hpp>
#include <vix/cli/errors/RawLogDetectors.hpp>
#include <vix/cli/ErrorHandler.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>

#ifndef _WIN32
#include <signal.h>
#endif

namespace vix::commands::RunCommand::detail
{
#ifndef VIX_CLI_VERSION
#define VIX_CLI_VERSION "dev"
#endif
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

    std::string path_list_separator()
    {
#ifdef _WIN32
      return ";";
#else
      return ":";
#endif
    }

    fs::path absolute_path_preserving_filename(const fs::path &path)
    {
      std::error_code ec;

      const fs::path absolute =
          fs::absolute(path, ec);

      if (ec)
        return path.lexically_normal();

      const fs::path parent =
          absolute.parent_path();

      if (parent.empty())
        return absolute.lexically_normal();

      ec.clear();

      const fs::path resolvedParent =
          fs::weakly_canonical(parent, ec);

      if (!ec)
      {
        return (
                   resolvedParent /
                   absolute.filename())
            .lexically_normal();
      }

      return absolute.lexically_normal();
    }

    std::string resolve_executable_path(const std::string &exe)
    {
      if (exe.empty())
        return exe;

      const fs::path exePath{exe};
      if (exePath.is_absolute() || exe.find('/') != std::string::npos
#ifdef _WIN32
          || exe.find('\\') != std::string::npos
#endif
      )
      {
        return absolute_path_preserving_filename(exePath).string();
      }

      const char *pathEnv = vix::utils::vix_getenv("PATH");
      if (!pathEnv || !*pathEnv)
        return exe;

      const std::string paths{pathEnv};
      const std::string sep = path_list_separator();
      std::size_t start = 0;

      while (start <= paths.size())
      {
        const std::size_t end = paths.find(sep, start);
        const std::string entry = paths.substr(
            start,
            end == std::string::npos ? std::string::npos : end - start);

        if (!entry.empty())
        {
          const fs::path candidate = fs::path(entry) / exe;
          std::error_code ec;
          if (fs::exists(candidate, ec) && !ec)
            return absolute_path_preserving_filename(candidate).string();

#ifdef _WIN32
          const fs::path exeCandidate = fs::path(entry) / (exe + ".exe");
          ec.clear();
          if (fs::exists(exeCandidate, ec) && !ec)
            return absolute_path_preserving_filename(exeCandidate).string();
#endif
        }

        if (end == std::string::npos)
          break;
        start = end + sep.size();
      }

      return exe;
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

    bool script_contains(const fs::path &p, std::string_view needle)
    {
      const std::string text = read_file_or_empty(p);
      return text.find(needle) != std::string::npos;
    }

    std::optional<fs::path> find_installed_vix_module_lib(const std::string &name)
    {
      std::vector<fs::path> prefixes;
      std::error_code ec;

      const char *home = vix::utils::vix_getenv(
#ifdef _WIN32
          "USERPROFILE"
#else
          "HOME"
#endif
      );

      if (home && *home)
        prefixes.push_back(fs::path(home) / ".vix" / "lib");

      prefixes.emplace_back("/usr/local/lib");
      prefixes.emplace_back("/usr/lib");

      for (const auto &prefix : prefixes)
      {
        const fs::path p = prefix / ("libvix_" + name + ".a");
        ec.clear();
        if (fs::exists(p, ec) && !ec)
          return p;
      }

      return std::nullopt;
    }

    void append_module_once(std::vector<std::string> &modules, const std::string &name)
    {
      if (std::find(modules.begin(), modules.end(), name) == modules.end())
        modules.push_back(name);
    }

    std::vector<fs::path> find_vix_direct_module_libs(const fs::path &scriptPath)
    {
      std::vector<std::string> modules;

      append_module_once(modules, "io");
      append_module_once(modules, "log");
      append_module_once(modules, "utils");
      append_module_once(modules, "error");

      if (script_contains(scriptPath, "<vix/fs") ||
          script_contains(scriptPath, "\"vix/fs"))
      {
        append_module_once(modules, "fs");
        append_module_once(modules, "path");
      }

      if (script_contains(scriptPath, "<vix/path") ||
          script_contains(scriptPath, "\"vix/path"))
      {
        append_module_once(modules, "path");
      }

      if (script_contains(scriptPath, "<vix/env") ||
          script_contains(scriptPath, "\"vix/env"))
      {
        append_module_once(modules, "env");
        append_module_once(modules, "path");
      }

      if (script_contains(scriptPath, "<vix/os") ||
          script_contains(scriptPath, "\"vix/os"))
      {
        append_module_once(modules, "os");
        append_module_once(modules, "path");
      }

      std::vector<fs::path> libs;
      for (const auto &module : modules)
      {
        if (const auto lib = find_installed_vix_module_lib(module))
          libs.push_back(*lib);
      }

      return libs;
    }

    void sort_unique(std::vector<std::string> &values)
    {
      std::sort(values.begin(), values.end());
      values.erase(std::unique(values.begin(), values.end()), values.end());
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
      return VIX_CLI_VERSION;
    }

    /**
     * @brief Run a compiler query and return a trimmed result.
     */
    std::string compiler_query(const std::string &compiler, const std::string &arg)
    {
      static std::unordered_map<std::string, std::string> cache;
      const std::string key = compiler + "\n" + arg;

      if (const auto it = cache.find(key); it != cache.end())
        return it->second;

      int code = 0;
      const std::string out = run_and_capture_with_code(
          process::quote(compiler) + " " + arg,
          code);

      if (code != 0)
      {
        cache[key] = "unknown";
        return "unknown";
      }

      std::string s = out;
      while (!s.empty() &&
             (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
      {
        s.pop_back();
      }

      if (s.empty())
        s = "unknown";

      cache[key] = s;
      return s;
    }

    bool direct_compiler_queries_enabled(const Options &opt)
    {
      return opt.compilerFingerprint == "strict";
    }

    /**
     * @brief Return the compiler version used by direct script mode.
     */
    std::string compiler_version_string(const std::string &compiler, const Options &opt)
    {
#ifdef _WIN32
      return "unknown";
#else
      if (!direct_compiler_queries_enabled(opt))
        return "fast";
      return compiler_query(compiler, "-dumpfullversion -dumpversion");
#endif
    }

    /**
     * @brief Return the compiler target triple used by direct script mode.
     */
    std::string compiler_target_triple(const std::string &compiler, const Options &opt)
    {
#ifdef _WIN32
      return "windows";
#else
      if (!direct_compiler_queries_enabled(opt))
        return "native";
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
      oss << ";san=" << text::bool01(want_any_sanitizer(opt.enableSanitizers, opt.enableUbsanOnly, opt.enableThreadSanitizer));

      oss << ";san_mode=" << sanitizer_mode_string(opt.enableSanitizers, opt.enableUbsanOnly, opt.enableThreadSanitizer);
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

    std::string header_content_fingerprint(const fs::path &p)
    {
      std::error_code ec;
      const fs::path abs = fs::absolute(p, ec).lexically_normal();
      return abs.string() + "|content=" + file_content_hash_hex(abs);
    }

    std::vector<std::string> collect_direct_header_fingerprints(
        const fs::path &cppPath,
        const ScriptProbeResult &probe,
        const std::string &compiler)
    {
      std::ostringstream cmd;
      cmd << process::quote(compiler) << " -MM";
      cmd << " -std=" << detect_cpp_standard(probe);

      for (const auto &inc : probe.includeDirs)
        cmd << " -I" << process::quote(inc);
      for (const auto &inc : probe.systemIncludeDirs)
        cmd << " -isystem " << process::quote(inc);
      for (const auto &def : probe.defines)
        cmd << " -D" << def;
      for (const auto &compileOpt : probe.compileOpts)
        append_quoted(cmd, compileOpt);

      cmd << " " << process::quote(cppPath.string());

      int exitCode = 0;
      const std::string depfile = run_and_capture_with_code(cmd.str(), exitCode);
      if (exitCode != 0)
        return {};

      std::vector<fs::path> paths;
      depfile_parse_paths(depfile, paths);

      std::vector<std::string> out;
      const fs::path source = fs::absolute(cppPath).lexically_normal();
      for (const fs::path &path : paths)
      {
        std::error_code ec;
        const fs::path absolute = fs::absolute(path, ec).lexically_normal();
        if (ec || absolute == source || !fs::is_regular_file(absolute, ec) || ec)
          continue;
        out.push_back(header_content_fingerprint(absolute));
      }

      std::sort(out.begin(), out.end());
      out.erase(std::unique(out.begin(), out.end()), out.end());
      return out;
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
            out.push_back(header_content_fingerprint(it->path()));
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
      const std::string compiler = resolve_executable_path(choose_cxx_compiler());

      DirectBuildFingerprint fp{};
      fp.formatVersion = "1";
      fp.vixVersion = vix_version_string();

      fp.compilerPath = compiler;
      fp.compilerVersion = compiler_version_string(compiler, opt);
      fp.targetTriple = compiler_target_triple(compiler, opt);

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
      if (probe.usesVixRuntime)
      {
        for (const auto &lib : find_vix_direct_module_libs(abs))
          fp.depFingerprints.push_back(path_fingerprint(lib));
      }
      fp.headerFingerprints = collect_direct_header_fingerprints(
          abs,
          probe,
          compiler);
      const auto dependencyHeaders = collect_header_fingerprints(probe);
      fp.headerFingerprints.insert(
          fp.headerFingerprints.end(),
          dependencyHeaders.begin(),
          dependencyHeaders.end());

      sort_unique(fp.includeDirs);
      sort_unique(fp.systemIncludeDirs);
      sort_unique(fp.defines);
      sort_unique(fp.compileOpts);
      sort_unique(fp.libDirs);
      sort_unique(fp.libs);
      sort_unique(fp.linkOpts);
      sort_unique(fp.depFingerprints);
      sort_unique(fp.headerFingerprints);

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
    std::string meta_value(const std::string &meta, std::string_view key)
    {
      const std::string prefix = std::string(key) + "=";
      const std::size_t start = meta.find(prefix);
      if (start == std::string::npos)
        return {};

      const std::size_t valueStart = start + prefix.size();
      const std::size_t end = meta.find('\n', valueStart);
      return meta.substr(
          valueStart,
          end == std::string::npos ? std::string::npos : end - valueStart);
    }

    std::string fingerprint_section(const std::string &meta)
    {
      const std::string marker = "\n[fingerprint]\n";
      const std::size_t pos = meta.find(marker);
      if (pos == std::string::npos)
        return {};

      return meta.substr(pos + marker.size());
    }

    std::string make_direct_failure_meta(
        const DirectScriptPlan &plan,
        int exitCode)
    {
      std::ostringstream oss;

      oss << "status=failed\n";
      oss << "exit_code=" << exitCode << "\n";
      oss << make_direct_cache_meta(
          plan.scriptPath,
          plan);

      return oss.str();
    }

    int cached_failure_exit_code(
        const std::string &meta)
    {
      const std::string value =
          meta_value(meta, "exit_code");

      if (value.empty())
        return 1;

      char *end = nullptr;

      const long parsed =
          std::strtol(
              value.c_str(),
              &end,
              10);

      if (end == value.c_str() ||
          end == nullptr ||
          *end != '\0' ||
          parsed <= 0 ||
          parsed > 255)
      {
        return 1;
      }

      return static_cast<int>(parsed);
    }

    bool load_direct_failure_cache(
        const DirectScriptPlan &plan,
        DirectScriptCacheState &cache)
    {
      if (!file_exists(cache.failureMetaFile))
        return false;

      if (!file_exists(cache.stdoutLogPath) ||
          !file_exists(cache.stderrLogPath))
      {
        return false;
      }

      const std::string meta =
          text::read_text_file_or_empty(
              cache.failureMetaFile);

      if (meta.empty())
        return false;

      if (meta_value(meta, "status") != "failed")
        return false;

      if (meta_value(meta, "cache_key") !=
          plan.cacheKey)
      {
        return false;
      }

      if (meta_value(meta, "script_content_hash") !=
          plan.fingerprint.scriptContentHash)
      {
        return false;
      }

      const std::string cachedFingerprint =
          fingerprint_section(meta);

      const std::string currentFingerprint =
          serialize_direct_build_fingerprint(
              plan.fingerprint);

      if (cachedFingerprint.empty() ||
          cachedFingerprint != currentFingerprint)
      {
        return false;
      }

      cache.cachedFailure = true;
      cache.cachedFailureExitCode =
          cached_failure_exit_code(meta);

      return true;
    }

    std::map<std::string, std::string> parse_key_value_lines(const std::string &text)
    {
      std::map<std::string, std::string> out;
      std::istringstream stream{text};
      std::string line;

      while (std::getline(stream, line))
      {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
          continue;

        out[line.substr(0, eq)] = line.substr(eq + 1);
      }

      return out;
    }

    std::string first_fingerprint_difference(
        const std::string &cached,
        const std::string &current)
    {
      const auto cachedMap = parse_key_value_lines(cached);
      const auto currentMap = parse_key_value_lines(current);

      for (const auto &[key, currentValue] : currentMap)
      {
        const auto it = cachedMap.find(key);
        if (it == cachedMap.end())
          return key + " added";
        if (it->second != currentValue)
          return key + " changed";
      }

      for (const auto &[key, cachedValue] : cachedMap)
      {
        (void)cachedValue;
        if (currentMap.find(key) == currentMap.end())
          return key + " removed";
      }

      return "fingerprint changed";
    }

    std::string direct_cache_rebuild_reason(
        const DirectScriptPlan &plan,
        const DirectScriptCacheState &cache)
    {
      if (!file_exists(plan.scriptPath))
        return "source file missing";

      if (!file_exists(plan.binaryPath))
        return "binary missing";

      if (!file_exists(cache.metaFile))
        return "metadata missing";

      const std::string meta = text::read_text_file_or_empty(cache.metaFile);
      if (meta.empty())
        return "metadata empty";

      const std::string wantKey = "cache_key=" + plan.cacheKey + "\n";
      if (meta.find(wantKey) == std::string::npos)
        return "cache key changed";

      const std::string wantContentHash =
          "script_content_hash=" + file_content_hash_hex(plan.scriptPath) + "\n";
      if (meta.find(wantContentHash) == std::string::npos)
        return "source content hash changed";

      const std::string cachedFingerprint = fingerprint_section(meta);
      const std::string currentFingerprint =
          serialize_direct_build_fingerprint(plan.fingerprint);

      if (cachedFingerprint.empty())
        return "fingerprint metadata missing";

      if (cachedFingerprint != currentFingerprint)
        return first_fingerprint_difference(cachedFingerprint, currentFingerprint);

      return {};
    }

    bool trace_direct_cache_enabled(const Options &opt)
    {
      if (opt.verbose)
        return true;

      return opt.traceCache;
    }

    std::string yes_no(bool value)
    {
      return value ? "yes" : "no";
    }

    void print_direct_cache_trace(
        const Options &opt,
        const DirectScriptPlan &plan,
        const DirectScriptCacheState &cache)
    {
      if (!trace_direct_cache_enabled(opt))
        return;

      const bool binaryExists =
          file_exists(plan.binaryPath);

      const fs::path activeMetaFile =
          cache.cachedFailure
              ? cache.failureMetaFile
              : cache.metaFile;

      const bool metadataExists =
          file_exists(activeMetaFile);

      std::string meta;

      if (metadataExists)
      {
        meta =
            text::read_text_file_or_empty(
                activeMetaFile);
      }

      const bool keyMatch =
          !meta.empty() && meta_value(meta, "cache_key") == plan.cacheKey;
      const bool mtimeMatch =
          !meta.empty() &&
          meta_value(meta, "script_mtime_ns") ==
              std::to_string(file_mtime_ns_local(plan.scriptPath));
      const bool hashMatch =
          !meta.empty() &&
          meta_value(meta, "script_content_hash") ==
              file_content_hash_hex(plan.scriptPath);
      const bool fingerprintMatch =
          !meta.empty() &&
          fingerprint_section(meta) ==
              serialize_direct_build_fingerprint(plan.fingerprint);

      std::cerr << "script strategy: direct\n";
      std::cerr << "cache key: " << plan.cacheKey << "\n";
      std::cerr << "cache dir: " << plan.cacheDir.string() << "\n";
      std::cerr << "binary exists: " << yes_no(binaryExists) << "\n";
      std::cerr << (cache.cachedFailure ? "failure metadata exists: " : "metadata exists: ") << yes_no(metadataExists) << "\n";
      std::cerr << "cached failure: " << yes_no(cache.cachedFailure) << "\n";
      std::cerr << "cache key match: " << yes_no(keyMatch) << "\n";
      std::cerr << "source mtime match: " << yes_no(mtimeMatch) << "\n";
      std::cerr << "source content hash match: " << yes_no(hashMatch) << "\n";
      std::cerr << "fingerprint match: " << yes_no(fingerprintMatch) << "\n";
      std::cerr << "direct PCH: ";
      if (const auto pch = find_vix_pch())
        std::cerr << pch->string() << "\n";
      else
        std::cerr << "unavailable\n";
      std::cerr << "rebuild reason: "
                << (cache.rebuildReason.empty() ? "cache hit" : cache.rebuildReason)
                << "\n";
    }

    /**
     * @brief Build the compile command for the direct path.
     */
    std::string make_direct_compile_cmd(const Options &opt, const DirectScriptPlan &plan)
    {
      std::ostringstream cmd;

      cmd << process::quote(plan.fingerprint.compilerPath);

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
          const auto libs = find_vix_direct_module_libs(plan.scriptPath);
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

        cmd << " -lspdlog -lfmt -pthread -ldl";
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

      const bool san = want_any_sanitizer(
          opt.enableSanitizers,
          opt.enableUbsanOnly,
          opt.enableThreadSanitizer);

      if (san)
      {
        if (opt.enableThreadSanitizer)
        {
          cmd << " -fsanitize=thread";
          cmd << " -O1";
          cmd << " -g";
          cmd << " -fno-omit-frame-pointer";
        }
        else if (opt.enableUbsanOnly)
        {
          cmd << " -fsanitize=undefined";
          cmd << " -g";
        }
        else
        {
          cmd << " -fsanitize=address,undefined";
          cmd << " -fno-omit-frame-pointer";
          cmd << " -g";
        }
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

    bool direct_replay_enabled(const Options &opt)
    {
      return opt.watch || opt.replay;
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

  DirectScriptCacheState load_direct_script_cache_state(
      const DirectScriptPlan &plan)
  {
    DirectScriptCacheState out{};

    out.rootDir = plan.cacheDir;
    out.binaryPath = plan.binaryPath;
    out.metaFile = plan.cacheDir / "meta.txt";
    out.failureMetaFile =
        plan.cacheDir / "failure.meta";

    out.stdoutLogPath =
        plan.cacheDir / "stdout.log";

    out.stderrLogPath =
        plan.cacheDir / "stderr.log";

    out.cacheKey = plan.cacheKey;

    out.rebuildReason.clear();
    out.cachedFailureExitCode = 0;
    out.cacheHit = false;
    out.cachedFailure = false;
    out.needsRebuild = true;

    /*
     * A previous compilation failure may keep its diagnostic payload,
     * but it must never suppress a future compilation attempt.
     *
     * Source files, generated headers, compiler state, or external
     * dependencies may have changed independently of the cached failure,
     * so failed builds are informational only and are not cache hits.
     */
    if (load_direct_failure_cache(
            plan,
            out))
    {
      out.cacheHit = false;
      out.cachedFailure = false;
      out.needsRebuild = true;
      out.cachedFailureExitCode = 0;
      out.rebuildReason =
          "previous compile failure";

      return out;
    }

    out.rebuildReason =
        direct_cache_rebuild_reason(
            plan,
            out);

    if (out.rebuildReason.empty())
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

    plan.compileCmd = make_direct_compile_cmd(opt, plan);
    plan.runCmd = make_direct_run_cmd(opt, plan);

    const auto cache = load_direct_script_cache_state(plan);
    plan.shouldCompile = cache.needsRebuild;
    print_direct_cache_trace(opt, plan, cache);

    return plan;
  }

  bool persist_direct_script_cache_metadata(
      const DirectScriptPlan &plan)
  {
    if (!file_exists(plan.binaryPath))
      return false;

    /*
     * A successful compilation supersedes a previous negative cache.
     */
    std::error_code ec;

    fs::remove(
        plan.cacheDir / "failure.meta",
        ec);

    ec.clear();

    fs::remove(
        plan.cacheDir / "stdout.log",
        ec);

    ec.clear();

    fs::remove(
        plan.cacheDir / "stderr.log",
        ec);

    const std::string meta =
        make_direct_cache_meta(
            plan.scriptPath,
            plan);

    return text::write_text_file(
        plan.cacheDir / "meta.txt",
        meta);
  }

  bool persist_direct_script_failure_cache(
      const DirectScriptPlan &plan,
      int exitCode,
      const std::string &stdoutText,
      const std::string &stderrText)
  {
    std::error_code ec;

    /*
     * Never leave an old successful artifact beside a failed state.
     */
    fs::remove(
        plan.binaryPath,
        ec);

    ec.clear();

    fs::remove(
        plan.cacheDir / "meta.txt",
        ec);

    ec.clear();

    /*
     * Write the diagnostic payload first and the marker last.
     *
     * This prevents an interrupted write from creating a valid-looking
     * negative cache without its diagnostic output.
     */
    if (!text::write_text_file(
            plan.cacheDir / "stdout.log",
            stdoutText))
    {
      return false;
    }

    if (!text::write_text_file(
            plan.cacheDir / "stderr.log",
            stderrText))
    {
      return false;
    }

    const std::string failureMeta =
        make_direct_failure_meta(
            plan,
            exitCode != 0 ? exitCode : 1);

    return text::write_text_file(
        plan.cacheDir / "failure.meta",
        failureMeta);
  }

  int replay_direct_script_cached_failure(
      const DirectScriptPlan &plan,
      const DirectScriptCacheState &cache)
  {
    const std::string stdoutText =
        text::read_text_file_or_empty(
            cache.stdoutLogPath);

    const std::string stderrText =
        text::read_text_file_or_empty(
            cache.stderrLogPath);

    const std::string compileLog =
        stdoutText + stderrText;

    bool handled = false;

    if (!compileLog.empty())
    {
      handled =
          vix::cli::ErrorHandler::printBuildErrors(
              compileLog,
              plan.scriptPath,
              "Script compile failed");
    }

    if (!handled)
      error("Script compile failed.");

    return cache.cachedFailureExitCode != 0
               ? cache.cachedFailureExitCode
               : 1;
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
    apply_sanitizer_env_if_needed(
        opt.enableSanitizers,
        opt.enableUbsanOnly,
        opt.enableThreadSanitizer);
#endif

    const auto cache =
        load_direct_script_cache_state(plan);

    if (cache.cachedFailure)
    {
      return replay_direct_script_cached_failure(
          plan,
          cache);
    }

    if (cache.needsRebuild)
    {
      const LiveRunResult build = run_cmd_live_filtered_capture(
          plan.compileCmd,
          "",
          false,
          0,
          want_any_sanitizer(
              opt.enableSanitizers,
              opt.enableUbsanOnly,
              opt.enableThreadSanitizer),
          true);

      if (build.exitCode != 0)
      {
        if (!persist_direct_script_failure_cache(
                plan,
                build.exitCode,
                build.stdoutText,
                build.stderrText))
        {
          std::cerr
              << "warning: unable to persist direct script failure cache\n";
        }

        bool handled = false;

        if (!build.stdoutText.empty() ||
            !build.stderrText.empty())
        {
          const std::string compileLog =
              build.stdoutText +
              build.stderrText;

          handled =
              vix::cli::ErrorHandler::printBuildErrors(
                  compileLog,
                  plan.scriptPath,
                  "Script compile failed");
        }

        if (!handled)
          error("Script compile failed.");

        return build.exitCode != 0
                   ? build.exitCode
                   : 1;
      }

      if (!persist_direct_script_cache_metadata(plan))
        std::cerr << "warning: unable to persist direct script cache metadata\n";
    }

    if (!plan.shouldRun)
      return 0;

    namespace replay = vix::commands::replay;

    replay::ReplayRecorder recorder;
    replay::ReplayCapture replayCapture;
    bool replayEnabled = false;

    if (direct_replay_enabled(opt))
    {
      replay::ReplayRecorderConfig replayConfig{};

      replayConfig.base_dir = fs::current_path();
      replayConfig.cwd = fs::current_path();
      replayConfig.project_dir = fs::current_path();
      replayConfig.target_path = plan.scriptPath;
      replayConfig.mode = opt.watch ? replay::ReplayMode::Dev : replay::ReplayMode::Run;
      replayConfig.target_kind = replay::ReplayTargetKind::SingleCpp;
      replayConfig.command = opt.watch
                                 ? "vix dev " + plan.scriptPath.string()
                                 : "vix run " + plan.scriptPath.string();
      replayConfig.resolved_command = plan.runCmd;
      replayConfig.vix_args = opt.scriptFlags;
      replayConfig.app_args = opt.runArgs;
      replayConfig.watch = opt.watch;
      replayConfig.direct_script = true;
      replayConfig.cmake_fallback = false;
      replayConfig.replayable = true;

      std::string replayErr;
      replayEnabled = recorder.begin(replayConfig, replayErr);

      if (replayEnabled)
        replayCapture.attach(&recorder);
    }

    const LiveRunResult run = run_cmd_live_filtered_capture(
        plan.runCmd,
        "Running script...",
        plan.passthroughRuntime,
        plan.effectiveTimeoutSec,
        want_any_sanitizer(
            opt.enableSanitizers,
            opt.enableUbsanOnly,
            opt.enableThreadSanitizer),
        false,
        replayEnabled ? &replayCapture : nullptr);

    if (replayEnabled)
    {
      replay::ReplayProcessResult process =
          replay::make_replay_process_result(
              run.exitCode,
              run.rawStatus,
              run.terminatedBySignal,
              run.termSignal);

      replay::ReplayCapturedResult captured =
          replay::make_replay_captured_result(
              replayCapture.output(),
              process);

      replay::ReplayRecorderFinish finish =
          replay::make_replay_finish_from_capture(captured);

      std::string finishErr;
      (void)recorder.finish(finish, finishErr);
    }

    if (is_user_interrupt_result(run))
    {
      hint("ℹ Program interrupted by user (SIGINT).");
      return 0;
    }

    bool handled = run.failureHandled;

    if (!handled && run.exitCode != 0)
    {
      std::string runtimeLog = run.stderrText;
      runtimeLog += run.stdoutText;

      if (!runtimeLog.empty())
      {
        handled = vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
            runtimeLog,
            plan.scriptPath,
            "run");
      }
    }

    handle_runtime_exit_code(run.exitCode, "run", handled);
    return run.exitCode;
  }

} // namespace vix::commands::RunCommand::detail
