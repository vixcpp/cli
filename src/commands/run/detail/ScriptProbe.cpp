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
#include <vix/cli/commands/run/RunScriptHelpers.hpp>
#include <vix/cli/cmake/GlobalPackages.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <iostream>
#include <unordered_set>

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

    fs::path find_script_project_root(const fs::path &cppFile)
    {
      std::error_code ec;

      fs::path current =
          cppFile.has_parent_path()
              ? fs::absolute(cppFile.parent_path(), ec).lexically_normal()
              : fs::current_path(ec).lexically_normal();

      if (ec)
        current = cppFile.parent_path();

      while (!current.empty())
      {
        if (fs::exists(current / "vix.lock") ||
            fs::exists(current / "vix.json") ||
            fs::exists(current / "vix.app") ||
            fs::exists(current / ".vix" / "vix_deps.cmake") ||
            fs::exists(current / ".vix" / "deps"))
        {
          return current;
        }

        const fs::path parent = current.parent_path();
        if (parent == current)
          break;

        current = parent;
      }

      return cppFile.has_parent_path()
                 ? fs::absolute(cppFile.parent_path()).lexically_normal()
                 : fs::current_path();
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
     * @brief Return true when the include path is a Vix runtime header.
     *
     * Generic detection: any include of the form <vix/...> or "vix/..." is
     * treated as a Vix runtime header, regardless of which sub-module it
     * belongs to. This avoids the need to maintain an exhaustive list of
     * known sub-module prefixes (vix/fs, vix/env, vix/error, vix/path, …)
     * and ensures that newly added modules are picked up automatically.
     *
     * The umbrella header <vix.hpp> / "vix.hpp" is also matched.
     */
    bool is_vix_runtime_include(const std::string &includePath)
    {
      if (includePath.empty())
        return false;

      // <vix.hpp> or "vix.hpp"
      if (includePath == "vix.hpp")
        return true;

      // <vix/...> or "vix/..." — any depth
      return starts_with(includePath, "vix/");
    }

    std::vector<std::string> extract_script_include_targets(const fs::path &cppPath)
    {
      std::vector<std::string> includes;

      std::ifstream ifs(cppPath);
      if (!ifs)
        return includes;

      std::string line;
      while (std::getline(ifs, line))
      {
        const auto include = parse_include_target(line);
        if (!include.has_value())
          continue;

        const std::string target = trim_copy(include->first);
        if (!target.empty())
          append_unique(includes, target);
      }

      return includes;
    }

    bool is_standard_library_include(const std::string &includePath)
    {
      if (includePath.empty())
        return false;

      if (includePath.find('/') != std::string::npos ||
          includePath.find('\\') != std::string::npos ||
          includePath.find('.') != std::string::npos)
      {
        return false;
      }

      static const std::unordered_set<std::string> headers = {
          "algorithm", "any", "array", "atomic", "barrier", "bit", "bitset",
          "cassert", "ccomplex", "cctype", "cerrno", "cfenv", "cfloat", "charconv",
          "chrono", "cinttypes", "climits", "clocale", "cmath", "codecvt", "compare",
          "complex", "concepts", "condition_variable", "coroutine", "csetjmp", "csignal",
          "cstdarg", "cstddef", "cstdint", "cstdio", "cstdlib", "cstring", "ctime",
          "deque", "exception", "execution", "expected", "filesystem", "format", "forward_list",
          "fstream", "functional", "future", "initializer_list", "iomanip", "ios", "iosfwd",
          "iostream", "istream", "iterator", "latch", "limits", "list", "locale", "map",
          "memory", "memory_resource", "mutex", "new", "numbers", "numeric", "optional",
          "ostream", "queue", "random", "ranges", "ratio", "regex", "scoped_allocator",
          "semaphore", "set", "shared_mutex", "source_location", "span", "sstream", "stack",
          "stdexcept", "stop_token", "streambuf", "string", "string_view", "strstream",
          "syncstream", "system_error", "thread", "tuple", "type_traits", "typeindex",
          "typeinfo", "unordered_map", "unordered_set", "utility", "valarray", "variant",
          "vector", "version"};

      return headers.find(includePath) != headers.end();
    }

    std::vector<std::string> extract_external_script_include_targets(const fs::path &cppPath)
    {
      std::vector<std::string> out;

      for (const auto &includeTarget : extract_script_include_targets(cppPath))
      {
        if (is_vix_runtime_include(includeTarget))
          continue;

        if (is_standard_library_include(includeTarget))
          continue;

        append_unique(out, includeTarget);
      }

      return out;
    }

    std::string dep_id_to_dir_name(std::string depId)
    {
      depId.erase(std::remove(depId.begin(), depId.end(), '@'), depId.end());
      std::replace(depId.begin(), depId.end(), '/', '.');
      return depId;
    }

    struct ProjectLockDependency
    {
      std::string id;
      fs::path sourcePath;
      bool headerOnly = false;
      std::vector<std::string> includeRoots;
    };

    void append_include_roots_from_json(std::vector<std::string> &out, const nlohmann::json &dep)
    {
      if (dep.contains("include") && dep["include"].is_string())
        append_unique(out, dep["include"].get<std::string>());

      if (dep.contains("includes") && dep["includes"].is_array())
      {
        for (const auto &item : dep["includes"])
        {
          if (item.is_string())
            append_unique(out, item.get<std::string>());
        }
      }
    }

    std::vector<ProjectLockDependency> load_project_lock_dependencies(const fs::path &projectRoot)
    {
      std::vector<ProjectLockDependency> deps;
      const fs::path lockPath = projectRoot / "vix.lock";
      if (!fs::exists(lockPath))
        return deps;

      std::ifstream ifs(lockPath);
      if (!ifs)
        return deps;

      nlohmann::json lock;
      try
      {
        ifs >> lock;
      }
      catch (...)
      {
        return deps;
      }

      if (!lock.is_object() || !lock.contains("dependencies") || !lock["dependencies"].is_array())
        return deps;

      const fs::path depsRoot = projectRoot / ".vix" / "deps";
      for (const auto &item : lock["dependencies"])
      {
        if (!item.is_object() || !item.contains("id") || !item["id"].is_string())
          continue;

        ProjectLockDependency dep;
        dep.id = item["id"].get<std::string>();
        dep.sourcePath = depsRoot / dep_id_to_dir_name(dep.id);

        if (item.contains("subdirectory") && item["subdirectory"].is_string())
        {
          const std::string subdir = item["subdirectory"].get<std::string>();
          if (!subdir.empty())
            dep.sourcePath /= subdir;
        }

        dep.headerOnly = item.value("header_only", false) || item.value("mode", std::string{}) == "header-only";
        append_include_roots_from_json(dep.includeRoots, item);

        if (dep.includeRoots.empty())
        {
          std::error_code ec;
          if (fs::exists(dep.sourcePath / "include", ec) && !ec)
            dep.includeRoots.push_back("include");
          else if (fs::exists(dep.sourcePath / "single_include", ec) && !ec)
            dep.includeRoots.push_back("single_include");
        }

        deps.push_back(std::move(dep));
      }

      return deps;
    }

    bool include_root_matches_target(const fs::path &includeRoot,
                                     const std::vector<std::string> &includeTargets)
    {
      std::error_code ec;
      if (!fs::exists(includeRoot, ec) || ec || !fs::is_directory(includeRoot, ec))
        return false;

      for (const auto &includeTarget : includeTargets)
      {
        if (includeTarget.empty())
          continue;

        ec.clear();
        if (fs::exists(includeRoot / includeTarget, ec) && !ec)
          return true;

        const auto slash = includeTarget.find('/');
        if (slash == std::string::npos)
          continue;

        const std::string prefix = includeTarget.substr(0, slash);
        if (prefix.empty())
          continue;

        ec.clear();
        if (fs::exists(includeRoot / prefix, ec) && !ec)
          return true;
      }

      return false;
    }

    bool apply_matching_project_dependency_includes(const fs::path &cppPath,
                                                    ScriptProbeResult &out)
    {
      const std::vector<std::string> includeTargets =
          extract_external_script_include_targets(cppPath);

      if (includeTargets.empty())
        return false;

      const fs::path projectRoot = find_script_project_root(cppPath);
      bool matchedCompiledDep = false;

      for (const auto &dep : load_project_lock_dependencies(projectRoot))
      {
        for (const auto &includeRootRelative : dep.includeRoots)
        {
          if (includeRootRelative.empty())
            continue;

          const fs::path includeRoot = dep.sourcePath / includeRootRelative;
          if (!include_root_matches_target(includeRoot, includeTargets))
            continue;

          append_unique(out.includeDirs, includeRoot.string());
          out.headerOnlyDepIncludeDirs.push_back(includeRoot);

          if (!dep.headerOnly)
          {
            out.requiresCMakeTargets = true;
            out.usesCompiledDeps = true;
            out.compiledDepPaths.push_back(dep.sourcePath);
            matchedCompiledDep = true;
          }

          break;
        }
      }

      return matchedCompiledDep;
    }

    bool global_package_matches_script_include(
        const vix::cli::build::GlobalPackage &pkg,
        const std::vector<std::string> &includeTargets)
    {
      if (pkg.installedPath.empty() || pkg.includeDir.empty())
        return false;

      const fs::path includeRoot = pkg.installedPath / pkg.includeDir;

      std::error_code ec;
      if (!fs::exists(includeRoot, ec) || ec)
        return false;

      ec.clear();
      if (!fs::is_directory(includeRoot, ec) || ec)
        return false;

      for (const auto &includeTarget : includeTargets)
      {
        if (includeTarget.empty())
          continue;

        ec.clear();
        if (fs::exists(includeRoot / includeTarget, ec) && !ec)
          return true;

        const auto slash = includeTarget.find('/');
        if (slash == std::string::npos)
          continue;

        const std::string prefix = includeTarget.substr(0, slash);
        if (prefix.empty())
          continue;

        ec.clear();
        if (fs::exists(includeRoot / prefix, ec) && !ec)
          return true;
      }

      return false;
    }

    bool global_package_requires_cmake_targets(
        const vix::cli::build::GlobalPackage &pkg)
    {
      std::error_code ec;
      const bool hasCMake =
          fs::exists(pkg.installedPath / "CMakeLists.txt", ec) && !ec;

      return hasCMake && pkg.type != "header-only";
    }

    void apply_matching_global_package_includes(
        const fs::path &cppPath,
        ScriptProbeResult &out)
    {
      const std::vector<std::string> includeTargets =
          extract_script_include_targets(cppPath);

      if (includeTargets.empty())
        return;

      for (const auto &pkg : vix::cli::build::load_global_packages())
      {
        if (!global_package_matches_script_include(pkg, includeTargets))
          continue;

        const fs::path includeRoot = pkg.installedPath / pkg.includeDir;
        append_unique(out.includeDirs, includeRoot.string());
        out.headerOnlyDepIncludeDirs.push_back(includeRoot);

        if (global_package_requires_cmake_targets(pkg))
        {
          out.requiresCMakeTargets = true;
          out.usesCompiledDeps = true;
          out.compiledDepPaths.push_back(pkg.installedPath);
        }
      }
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

      // Vix runtime header detection — generic approach
      //
      // Any #include <vix/...> or #include "vix/..." is treated as a Vix
      // runtime include, regardless of which sub-module it targets.
      // This covers every existing and future module (fs, env, error, path,
      // utils, time, validation, conversion, template, tests, …) without
      // requiring an exhaustive, manually-maintained list.
      const auto parsed_inc = parse_include_target(line);
      const bool uses_vix_runtime_header = parsed_inc.has_value() &&
                                           is_vix_runtime_include(parsed_inc->first);

      // Vix runtime symbol detection — any use of vix:: / Vix:: namespace
      const bool uses_vix_runtime_symbol =
          has("vix::") || has("Vix::");

      if (uses_vix_runtime_header || uses_vix_runtime_symbol)
      {
        f.usesVix = true;
      }

      // ORM detection — vix/orm sub-tree or vix::orm namespace
      if (has("#include <vix/orm") || has("#include \"vix/orm") ||
          has("vix::orm") || has("using namespace vix::orm"))
      {
        f.usesOrm = true;
      }

      // DB detection — vix/db sub-tree or vix::db namespace
      if (has("#include <vix/db") || has("#include \"vix/db") ||
          has("vix::db") || has("using namespace vix::db"))
      {
        f.usesDb = true;
      }

      // MySQL detection — factory helpers, engine enum, connector headers
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

    if (out.features.usesVix)
    {
      out.strategy = ScriptExecutionStrategy::CMakeFallback;
      out.fallbackReason = ScriptFallbackReason::UsesVixRuntime;
      out.canUseDirectCompile = false;
      out.shouldUseCMakeFallback = true;
      return out;
    }

    out.compileFlags = parse_compile_flags(opt.scriptFlags);
    out.linkFlags = parse_link_flags(opt.scriptFlags);

    out.includeDirs = out.compileFlags.includeDirs;
    out.systemIncludeDirs = out.compileFlags.systemIncludeDirs;
    out.defines = out.compileFlags.defines;
    out.compileOpts = out.compileFlags.compileOpts;

    out.libDirs = out.linkFlags.libDirs;
    out.libs = out.linkFlags.libs;
    out.linkOpts = out.linkFlags.linkOpts;

    apply_matching_project_dependency_includes(opt.cppFile, out);
    apply_matching_global_package_includes(opt.cppFile, out);

    const bool unsupportedFlags =
        script_flags_require_cmake_fallback(opt.scriptFlags);

    const bool dependencyManagedIncludes =
        include_dirs_require_cmake_fallback(
            out.includeDirs,
            out.systemIncludeDirs);

    out.requiresCMakeTargets =
        out.requiresCMakeTargets ||
        out.features.usesVix ||
        out.features.usesOrm ||
        out.features.usesDb ||
        out.features.usesMySql ||
        opt.withSqlite ||
        opt.withMySql ||
        !out.libs.empty() ||
        !out.libDirs.empty() ||
        !out.linkOpts.empty();

    out.usesCompiledDeps =
        out.usesCompiledDeps ||
        !out.libs.empty() ||
        !out.libDirs.empty() ||
        !out.linkOpts.empty();

    const bool vixInstalledLocally =
        out.features.usesVix &&
        find_vix_include_dir().has_value() &&
        (find_vix_lib().has_value() || !find_vix_all_module_libs().empty());

    const bool onlyVixIncludesAdded =
        vixInstalledLocally &&
        !dependencyManagedIncludes &&
        out.libs.empty() &&
        out.libDirs.empty() &&
        out.linkOpts.empty();

    if (out.usesCompiledDeps && !onlyVixIncludesAdded)
      out.compiledDepPaths.push_back(opt.cppFile.parent_path() / ".vix" / "deps");

    const bool requiresFullBuildSystem =
        out.features.usesOrm ||
        out.features.usesDb ||
        out.features.usesMySql ||
        opt.withSqlite ||
        opt.withMySql;

    if (requiresFullBuildSystem)
    {
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

    const bool allowDirect =
        !unsupportedFlags &&
        (!out.usesCompiledDeps || onlyVixIncludesAdded) &&
        (!out.requiresCMakeTargets || vixInstalledLocally);

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
