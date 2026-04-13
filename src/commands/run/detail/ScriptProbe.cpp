/**
 *
 *  @file ScriptProbe.cpp
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
#include <vix/cli/commands/run/detail/ScriptProbe.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vix::commands::RunCommand::detail
{
  namespace
  {
    /**
     * @brief Return a trimmed copy of the input string.
     */
    std::string trim_copy(std::string s)
    {
      auto is_ws = [](unsigned char c)
      { return std::isspace(c) != 0; };

      while (!s.empty() && is_ws(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());

      while (!s.empty() && is_ws(static_cast<unsigned char>(s.back())))
        s.pop_back();

      return s;
    }

    /**
     * @brief Return true when a string starts with the given prefix.
     */
    bool starts_with(const std::string &s, const char *prefix)
    {
      return s.rfind(prefix, 0) == 0;
    }

    /**
     * @brief Append a string only once.
     */
    void append_unique(std::vector<std::string> &out, const std::string &value)
    {
      if (value.empty())
        return;

      if (std::find(out.begin(), out.end(), value) == out.end())
        out.push_back(value);
    }

    /**
     * @brief Extract an include target from a #include line.
     *
     * Returns:
     * - the included path
     * - whether it is a quoted include
     */
    std::optional<std::pair<std::string, bool>> parse_include_target(const std::string &line)
    {
      if (line.find("#include") == std::string::npos)
        return std::nullopt;

      const std::size_t angleStart = line.find('<');
      const std::size_t quoteStart = line.find('"');

      if (quoteStart != std::string::npos &&
          (angleStart == std::string::npos || quoteStart < angleStart))
      {
        const std::size_t end = line.find('"', quoteStart + 1);
        if (end == std::string::npos || end <= quoteStart + 1)
          return std::nullopt;

        return std::make_pair(line.substr(quoteStart + 1, end - quoteStart - 1), true);
      }

      if (angleStart != std::string::npos)
      {
        const std::size_t end = line.find('>', angleStart + 1);
        if (end == std::string::npos || end <= angleStart + 1)
          return std::nullopt;

        return std::make_pair(line.substr(angleStart + 1, end - angleStart - 1), false);
      }

      return std::nullopt;
    }

    /**
     * @brief Return true when the include clearly looks project-managed.
     *
     * Direct script mode is intentionally strict:
     * - quoted includes are treated as project headers
     * - includes containing a path separator are treated as project layout
     */
    bool include_path_requires_fallback(const std::string &includePath, bool isQuotedInclude)
    {
      if (includePath.empty())
        return false;

      if (isQuotedInclude)
        return true;

      if (includePath.find('/') != std::string::npos ||
          includePath.find('\\') != std::string::npos)
      {
        return true;
      }

      return false;
    }

    /**
     * @brief Return true when the script source layout is not suitable for direct mode.
     *
     * This does not try to recognize specific libraries.
     * It simply distinguishes standalone scripts from project-like sources.
     */
    bool script_source_requires_cmake_fallback(const fs::path &cppPath)
    {
      std::ifstream ifs(cppPath);
      if (!ifs)
        return false;

      std::string line;
      while (std::getline(ifs, line))
      {
        const auto include = parse_include_target(line);
        if (!include.has_value())
          continue;

        const auto &[includePath, isQuotedInclude] = *include;
        if (include_path_requires_fallback(includePath, isQuotedInclude))
          return true;
      }

      return false;
    }

    /**
     * @brief Return true when forwarded include paths suggest dependency-managed code.
     */
    bool include_dirs_require_cmake_fallback(const std::vector<std::string> &includeDirs,
                                             const std::vector<std::string> &systemIncludeDirs)
    {
      auto has_vix_deps = [](const std::string &p) -> bool
      {
#ifdef _WIN32
        return p.find("\\.vix\\deps\\") != std::string::npos ||
               p.find("/.vix/deps/") != std::string::npos;
#else
        return p.find("/.vix/deps/") != std::string::npos;
#endif
      };

      for (const auto &p : includeDirs)
      {
        if (has_vix_deps(p))
          return true;
      }

      for (const auto &p : systemIncludeDirs)
      {
        if (has_vix_deps(p))
          return true;
      }

      return false;
    }

    /**
     * @brief Return true when the script explicitly forwards include roots.
     *
     * A truly standalone single-file direct compile should not need project include roots.
     */
    bool explicit_include_roots_require_fallback(const std::vector<std::string> &includeDirs,
                                                 const std::vector<std::string> &systemIncludeDirs)
    {
      return !includeDirs.empty() || !systemIncludeDirs.empty();
    }

    /**
     * @brief Return true when the forwarded flag is unsupported by the direct path.
     *
     * The direct path stays intentionally strict. Any ambiguous build-system-like
     * flag or compilation mode switch should force CMake fallback instead.
     */
    bool is_unsupported_direct_flag(const std::string &flag)
    {
      if (flag.empty())
        return false;

      if (flag == "-c" || flag == "-S" || flag == "-E")
        return true;

      if (flag == "-shared" || flag == "-static")
        return true;

      if (flag == "-Winvalid-pch")
        return true;

      if (starts_with(flag, "-M") || starts_with(flag, "/showIncludes"))
        return true;

      if (flag.size() > 2 &&
          (flag.ends_with(".o") ||
           flag.ends_with(".obj") ||
           flag.ends_with(".a") ||
           flag.ends_with(".lib") ||
           flag.ends_with(".so") ||
           flag.ends_with(".dylib")))
      {
        return true;
      }

      return false;
    }

    /**
     * @brief Return true when the flag is a compile option we can safely keep.
     */
    bool is_supported_compile_opt(const std::string &flag)
    {
      if (flag.empty())
        return false;

      if (flag == "-pthread")
        return true;

      if (starts_with(flag, "-std=") ||
          starts_with(flag, "-O") ||
          starts_with(flag, "-g") ||
          starts_with(flag, "-W") ||
          starts_with(flag, "-f") ||
          starts_with(flag, "-m") ||
          starts_with(flag, "-pipe") ||
          starts_with(flag, "-pedantic"))
      {
        return true;
      }

      return false;
    }

    /**
     * @brief Return true when the flag is a link option we can safely keep.
     */
    bool is_supported_link_opt(const std::string &flag)
    {
      if (flag.empty())
        return false;

      if (flag == "-pthread")
        return true;

      if (starts_with(flag, "-Wl,") ||
          starts_with(flag, "-fuse-ld=") ||
          starts_with(flag, "-fsanitize="))
      {
        return true;
      }

      return false;
    }

    /**
     * @brief Return whether the parsed flag sets contain unsupported direct flags.
     */
    bool script_flags_require_cmake_fallback(const std::vector<std::string> &flags)
    {
      for (const auto &flag : flags)
      {
        if (is_unsupported_direct_flag(flag))
          return true;
      }

      return false;
    }

    /**
     * @brief Normalize the fallback reason chosen from probe state.
     */
    ScriptFallbackReason choose_fallback_reason(
        const ScriptFeatures &features,
        bool usesCompiledDeps,
        bool requiresCMakeTargets,
        bool unsupportedFlags)
    {
      if (features.usesOrm)
        return ScriptFallbackReason::UsesOrm;

      if (features.usesDb)
        return ScriptFallbackReason::UsesDatabase;

      if (features.usesMySql)
        return ScriptFallbackReason::UsesMySql;

      if (requiresCMakeTargets)
        return ScriptFallbackReason::RequiresCMakeTargets;

      if (features.usesVix)
        return ScriptFallbackReason::UsesVixRuntime;

      if (usesCompiledDeps)
        return ScriptFallbackReason::UsesCompiledDeps;

      if (unsupportedFlags)
        return ScriptFallbackReason::UnsupportedFlags;

      return ScriptFallbackReason::Unknown;
    }

  } // namespace

  bool script_uses_vix(const fs::path &cppPath)
  {
    const auto f = detect_script_features(cppPath);
    return f.usesVix;
  }

  ScriptFeatures detect_script_features(const fs::path &cppPath)
  {
    ScriptFeatures f{};

    std::ifstream ifs(cppPath);
    if (!ifs)
      return f;

    std::string line;
    while (std::getline(ifs, line))
    {
      auto has = [&](const char *s)
      { return line.find(s) != std::string::npos; };

      if (has("vix::") ||
          has("Vix::") ||
          has("using namespace vix") ||
          has("using namespace Vix") ||
          has("using vix::") ||
          has("using Vix::") ||
          has("#include <vix/") ||
          has("#include \"vix/"))
      {
        f.usesVix = true;
      }

      if (has("#include <vix/orm/") || has("#include \"vix/orm/") ||
          has("vix::orm") || has("using namespace vix::orm"))
      {
        f.usesOrm = true;
      }

      if (has("#include <vix/db/") || has("#include \"vix/db/") ||
          has("vix::db") || has("using namespace vix::db"))
      {
        f.usesDb = true;
      }

      if (has("make_mysql_factory") ||
          has("Engine::MySQL") ||
          has("mysqlcppconn") ||
          has("#include <mysql") ||
          has("#include \"mysql"))
      {
        f.usesMySql = true;
      }

      if (!f.usesMySql &&
          (has("tcp://") || has(":3306") || has("MySQL")))
      {
        f.usesMySql = true;
      }
    }

    if (f.usesOrm)
      f.usesDb = true;

    return f;
  }

  ScriptCompileFlags parse_compile_flags(const std::vector<std::string> &flags)
  {
    ScriptCompileFlags out;

    for (std::size_t i = 0; i < flags.size(); ++i)
    {
      const std::string &f = flags[i];

      if (f.empty())
        continue;

      if (starts_with(f, "-I") && f.size() > 2)
      {
        append_unique(out.includeDirs, f.substr(2));
        continue;
      }

      if (f == "-I")
      {
        if (i + 1 < flags.size())
          append_unique(out.includeDirs, flags[++i]);
        continue;
      }

      if (starts_with(f, "-isystem") && f.size() > 8)
      {
        append_unique(out.systemIncludeDirs, trim_copy(f.substr(8)));
        continue;
      }

      if (f == "-isystem")
      {
        if (i + 1 < flags.size())
          append_unique(out.systemIncludeDirs, flags[++i]);
        continue;
      }

      if (starts_with(f, "-D") && f.size() > 2)
      {
        append_unique(out.defines, f.substr(2));
        continue;
      }

      if (f == "-D")
      {
        if (i + 1 < flags.size())
          append_unique(out.defines, flags[++i]);
        continue;
      }

      if (is_supported_compile_opt(f))
      {
        append_unique(out.compileOpts, f);
        continue;
      }
    }

    return out;
  }

  ScriptLinkFlags parse_link_flags(const std::vector<std::string> &flags)
  {
    ScriptLinkFlags out;

    for (std::size_t i = 0; i < flags.size(); ++i)
    {
      const std::string &f = flags[i];

      if (f.empty())
        continue;

      if (starts_with(f, "-l") && f.size() > 2)
      {
        append_unique(out.libs, f.substr(2));
        continue;
      }

      if (f == "-l")
      {
        if (i + 1 < flags.size())
          append_unique(out.libs, flags[++i]);
        continue;
      }

      if (starts_with(f, "-L") && f.size() > 2)
      {
        append_unique(out.libDirs, f.substr(2));
        continue;
      }

      if (f == "-L")
      {
        if (i + 1 < flags.size())
          append_unique(out.libDirs, flags[++i]);
        continue;
      }

      if (is_supported_link_opt(f))
      {
        append_unique(out.linkOpts, f);
        continue;
      }
    }

    return out;
  }

  ScriptProbeResult probe_single_cpp_script(const Options &opt)
  {
    ScriptProbeResult out{};

    if (!opt.singleCpp || opt.cppFile.empty())
    {
      out.strategy = ScriptExecutionStrategy::CMakeFallback;
      out.fallbackReason = ScriptFallbackReason::UnsupportedLayout;
      out.shouldUseCMakeFallback = true;
      return out;
    }

    out.features = detect_script_features(opt.cppFile);
    out.usesVixRuntime = out.features.usesVix;

    out.compileFlags = parse_compile_flags(opt.scriptFlags);
    out.linkFlags = parse_link_flags(opt.scriptFlags);

    out.includeDirs = out.compileFlags.includeDirs;
    out.systemIncludeDirs = out.compileFlags.systemIncludeDirs;
    out.defines = out.compileFlags.defines;
    out.compileOpts = out.compileFlags.compileOpts;

    out.libDirs = out.linkFlags.libDirs;
    out.libs = out.linkFlags.libs;
    out.linkOpts = out.linkFlags.linkOpts;

    const bool unsupportedFlags =
        script_flags_require_cmake_fallback(opt.scriptFlags);

    const bool sourceLayoutRequiresFallback =
        script_source_requires_cmake_fallback(opt.cppFile);

    const bool dependencyManagedIncludes =
        include_dirs_require_cmake_fallback(
            out.includeDirs,
            out.systemIncludeDirs);

    const bool explicitIncludeRoots =
        explicit_include_roots_require_fallback(
            out.includeDirs,
            out.systemIncludeDirs);

    out.requiresCMakeTargets =
        out.features.usesVix ||
        out.features.usesOrm ||
        out.features.usesDb ||
        out.features.usesMySql ||
        sourceLayoutRequiresFallback ||
        dependencyManagedIncludes ||
        explicitIncludeRoots ||
        opt.withSqlite ||
        opt.withMySql ||
        !out.libs.empty() ||
        !out.libDirs.empty() ||
        !out.linkOpts.empty();

    out.usesCompiledDeps =
        dependencyManagedIncludes ||
        explicitIncludeRoots ||
        !out.libs.empty() ||
        !out.libDirs.empty() ||
        !out.linkOpts.empty();

    if (out.usesCompiledDeps)
      out.compiledDepPaths.push_back(opt.cppFile.parent_path() / ".vix" / "deps");

    const bool allowDirect =
        !unsupportedFlags &&
        !out.requiresCMakeTargets &&
        !out.usesCompiledDeps;

    if (allowDirect)
    {
      out.strategy = ScriptExecutionStrategy::Direct;
      out.fallbackReason = ScriptFallbackReason::None;
      out.canUseDirectCompile = true;
      out.shouldUseCMakeFallback = false;
      return out;
    }

    out.strategy = ScriptExecutionStrategy::CMakeFallback;
    out.fallbackReason = choose_fallback_reason(
        out.features,
        out.usesCompiledDeps,
        out.requiresCMakeTargets,
        unsupportedFlags);

    out.canUseDirectCompile = false;
    out.shouldUseCMakeFallback = true;

    return out;
  }

} // namespace vix::commands::RunCommand::detail
