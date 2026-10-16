/**
 *
 *  @file ScriptCMake.cpp
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
#include <vix/cli/commands/run/detail/ScriptCMake.hpp>
#include <vix/cli/commands/run/detail/ScriptProbe.hpp>
#include <vix/env/Legacy.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vix::commands::RunCommand::detail
{
  namespace
  {
    struct ResolvedScriptDeps
    {
      std::vector<std::string> orderedDepIds;
      std::vector<std::pair<std::string, fs::path>> uniqueCmakeDeps;
      std::vector<std::string> cmakeDepAliases;
      std::vector<std::string> includeDirs;
    };

    std::string dep_dir_to_id(const std::string &pkgDir)
    {
      std::string id = pkgDir;
      std::replace(id.begin(), id.end(), '.', '/');
      return id;
    }

    std::string dep_id_to_dir(std::string depId)
    {
      depId.erase(std::remove(depId.begin(), depId.end(), '@'), depId.end());
      std::replace(depId.begin(), depId.end(), '/', '.');
      return depId;
    }

    fs::path find_script_project_root(const fs::path &cppPath)
    {
      std::error_code ec;

      fs::path current =
          cppPath.has_parent_path()
              ? fs::absolute(cppPath.parent_path(), ec).lexically_normal()
              : fs::current_path(ec).lexically_normal();

      if (ec)
      {
        current = cppPath.parent_path();
      }

      while (!current.empty())
      {
        if (fs::exists(current / "vix.toml") ||
            fs::exists(current / "vix.lock") ||
            fs::exists(current / "CMakeLists.txt") ||
            fs::exists(current / ".vix" / "vix_deps.cmake") ||
            fs::exists(current / ".vix" / "deps"))
        {
          return current;
        }

        const fs::path parent = current.parent_path();

        if (parent == current)
        {
          break;
        }

        current = parent;
      }

      return cppPath.has_parent_path()
                 ? fs::absolute(cppPath.parent_path()).lexically_normal()
                 : fs::current_path();
    }

    std::optional<std::string> home_dir()
    {
#ifdef _WIN32
      const char *home = vix::env::legacy::getenv("USERPROFILE");
#else
      const char *home = vix::env::legacy::getenv("HOME");
#endif
      if (!home || std::string(home).empty())
        return std::nullopt;

      return std::string(home);
    }

    std::string sanitize_identifier(std::string s)
    {
      if (s.empty())
        return "script";

      for (char &c : s)
      {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '_'))
          c = '_';
      }

      if (!std::isalpha(static_cast<unsigned char>(s.front())) && s.front() != '_')
        s.insert(s.begin(), '_');

      return s;
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

    std::string make_unique_script_target_name(const std::string &exeName, const fs::path &cppPath)
    {
      const std::string safeExe = sanitize_identifier(exeName);
      const std::string absolutePath = fs::absolute(cppPath).lexically_normal().string();
      return "vix_run__" + safeExe + "__" + hex_u64(fnv1a_64(absolutePath));
    }

    std::string make_unique_project_name(const std::string &exeName, const fs::path &cppPath)
    {
      const std::string safeExe = sanitize_identifier(exeName);
      const std::string absolutePath = fs::absolute(cppPath).lexically_normal().string();
      return "vix_script_" + safeExe + "_" + hex_u64(fnv1a_64("project:" + absolutePath));
    }

    void append_unique_string(std::vector<std::string> &items, const std::string &value)
    {
      if (value.empty())
        return;

      if (std::find(items.begin(), items.end(), value) == items.end())
        items.push_back(value);
    }


    std::string dep_id_to_cmake_alias(const std::string &id)
    {
      std::string alias = id;
      std::replace(alias.begin(), alias.end(), '/', ':');
      const auto pos = alias.find(':');
      if (pos != std::string::npos)
        alias.replace(pos, 1, "::");

      return alias;
    }

    std::vector<std::string> load_ordered_packages_from_lock(const fs::path &lockPath)
    {
      std::vector<std::string> ordered;

      if (!fs::exists(lockPath))
        return ordered;

      std::ifstream ifs(lockPath);
      if (!ifs)
        return ordered;

      nlohmann::json j;
      ifs >> j;

      if (j.is_array())
      {
        for (const auto &pkg : j)
        {
          if (!pkg.is_object())
            continue;

          if (pkg.contains("id") && pkg["id"].is_string())
            ordered.push_back(pkg["id"].get<std::string>());
        }
        return ordered;
      }

      if (j.is_object())
      {
        if (j.contains("packages") && j["packages"].is_array())
        {
          for (const auto &pkg : j["packages"])
          {
            if (!pkg.is_object())
              continue;

            if (pkg.contains("id") && pkg["id"].is_string())
              ordered.push_back(pkg["id"].get<std::string>());
          }

          return ordered;
        }

        if (j.contains("dependencies") && j["dependencies"].is_array())
        {
          for (const auto &dep : j["dependencies"])
          {
            if (dep.is_string())
            {
              ordered.push_back(dep.get<std::string>());
            }
            else if (dep.is_object())
            {
              if (dep.contains("id") && dep["id"].is_string())
                ordered.push_back(dep["id"].get<std::string>());
            }
          }

          return ordered;
        }

        if (j.contains("dependencies") && j["dependencies"].is_object())
        {
          for (auto it = j["dependencies"].begin(); it != j["dependencies"].end(); ++it)
            ordered.push_back(it.key());

          return ordered;
        }
      }

      return ordered;
    }

    std::vector<std::string> lock_package_ids_to_aliases(
        const std::vector<std::string> &packageIds)
    {
      std::vector<std::string> aliases;

      for (const auto &id : packageIds)
      {
        if (id.empty())
          continue;

        append_unique_string(aliases, dep_id_to_cmake_alias(id));
      }

      std::sort(aliases.begin(), aliases.end());
      aliases.erase(std::unique(aliases.begin(), aliases.end()), aliases.end());

      return aliases;
    }

    std::vector<std::pair<std::string, std::string>> extract_dep_packages_from_include_dirs(
        const std::vector<std::string> &includeDirs)
    {
      std::vector<std::pair<std::string, std::string>> out;

      for (const auto &d : includeDirs)
      {
        const std::string marker = "/.vix/deps/";
        const auto pos = d.find(marker);
        if (pos == std::string::npos)
          continue;

        const std::string depsRoot = d.substr(0, pos + marker.size());
        const std::string tail = d.substr(pos + marker.size());

        const auto slash = tail.find('/');
        if (slash == std::string::npos)
          continue;

        const std::string pkgDir = tail.substr(0, slash);

        bool exists = false;
        for (const auto &it : out)
        {
          if (it.first == pkgDir)
          {
            exists = true;
            break;
          }
        }

        if (!exists)
          out.push_back({pkgDir, depsRoot + pkgDir});
      }

      return out;
    }

    ResolvedScriptDeps resolve_script_deps(
        const fs::path &cppPath,
        ScriptCompileFlags &cf)
    {
      ResolvedScriptDeps resolved;

      const fs::path projectRoot = find_script_project_root(cppPath);
      const fs::path depsRoot = projectRoot / ".vix" / "deps";
      const fs::path lockPath = projectRoot / "vix.lock";

      resolved.orderedDepIds = load_ordered_packages_from_lock(lockPath);

      if (resolved.orderedDepIds.empty())
      {
        const auto depPackages = extract_dep_packages_from_include_dirs(cf.includeDirs);
        for (const auto &[pkgDir, _pkgPath] : depPackages)
          resolved.orderedDepIds.push_back(pkgDir);
      }

      std::vector<std::pair<std::string, fs::path>> cmakeDeps;
      for (const auto &depId : resolved.orderedDepIds)
      {
        std::string pkgDir = depId;
        fs::path pkgPath;

        if (depId.find('/') != std::string::npos || depId.find('@') != std::string::npos)
        {
          pkgDir = dep_id_to_dir(depId);
          pkgPath = depsRoot / pkgDir;
        }
        else
        {
          pkgPath = depsRoot / depId;
        }

        if (fs::exists(pkgPath / "CMakeLists.txt"))
        {
          cmakeDeps.emplace_back(pkgDir, pkgPath);
        }
        else
        {
          const fs::path includeGuess = pkgPath / "include";
          if (fs::exists(includeGuess))
            append_unique_string(cf.includeDirs, includeGuess.string());
        }
      }

      std::unordered_set<std::string> seenPkgDirs;
      for (const auto &[pkgDir, pkgPath] : cmakeDeps)
      {
        if (seenPkgDirs.insert(pkgDir).second)
          resolved.uniqueCmakeDeps.emplace_back(pkgDir, pkgPath);
      }

      for (const auto &[pkgDir, _pkgPath] : resolved.uniqueCmakeDeps)
        append_unique_string(resolved.cmakeDepAliases, dep_id_to_cmake_alias(dep_dir_to_id(pkgDir)));

      std::sort(resolved.cmakeDepAliases.begin(), resolved.cmakeDepAliases.end());
      resolved.cmakeDepAliases.erase(
          std::unique(resolved.cmakeDepAliases.begin(), resolved.cmakeDepAliases.end()),
          resolved.cmakeDepAliases.end());

      resolved.includeDirs = cf.includeDirs;
      return resolved;
    }

    void append_line(std::string &s, const std::string &line = {})
    {
      s += line;
      s += '\n';
    }

    std::string cmake_quote(const std::string &p)
    {
      std::string out = "\"";
      for (char c : p)
      {
        if (c == '\\')
          out += "\\\\";
        else if (c == '"')
          out += "\\\"";
        else
          out += c;
      }
      out += "\"";
      return out;
    }

    fs::path local_vix_deps_cmake_path(const fs::path &cppPath)
    {
      const fs::path projectRoot = find_script_project_root(cppPath);
      return projectRoot / ".vix" / "vix_deps.cmake";
    }

    bool has_local_vix_deps_cmake(const fs::path &cppPath)
    {
      const fs::path projectRoot = find_script_project_root(cppPath);
      return fs::exists(projectRoot / ".vix" / "vix_deps.cmake");
    }

    void append_local_vix_deps_include(
        std::string &s,
        const fs::path &cppPath)
    {
      const fs::path depsCmake = local_vix_deps_cmake_path(cppPath);

      if (!fs::exists(depsCmake))
        return;

      append_line(s, "# Local Vix registry dependencies");
      append_line(s, "if (EXISTS " + cmake_quote(depsCmake.string()) + ")");
      append_line(s, "  include(" + cmake_quote(depsCmake.string()) + ")");
      append_line(s, "endif()");
      append_line(s);
    }

    std::string cmake_quote_raw(const std::string &p)
    {
      return "\"" + p + "\"";
    }

    void append_generated_helpers(std::string &s)
    {
      append_line(s, "# ------------------------------------------------------");
      append_line(s, "# Internal helpers generated by Vix");
      append_line(s, "# ------------------------------------------------------");
      append_line(s);

      append_line(s, "function(_vix_disable_dep_extras dep_ns dep_name)");
      append_line(s, "  string(TOUPPER \"${dep_ns}\" _VIX_NS_UPPER)");
      append_line(s, "  string(TOUPPER \"${dep_name}\" _VIX_NAME_UPPER)");
      append_line(s);
      append_line(s, "  set(BUILD_TESTING OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(BUILD_TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(ENABLE_TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(UNIT_TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(BUILD_EXAMPLES OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(ENABLE_EXAMPLES OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(EXAMPLES OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(BUILD_BENCHMARKS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(BENCHMARKS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(BUILD_DOCS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(ENABLE_DOCS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(DOCS OFF CACHE BOOL \"\" FORCE)");
      append_line(s);
      append_line(s, "  set(${_VIX_NS_UPPER}_BUILD_TESTING OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_BUILD_TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_ENABLE_TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_UNIT_TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_BUILD_EXAMPLES OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_ENABLE_EXAMPLES OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_EXAMPLES OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_BUILD_BENCHMARKS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_BENCHMARKS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_BUILD_DOCS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_ENABLE_DOCS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_DOCS OFF CACHE BOOL \"\" FORCE)");
      append_line(s);
      append_line(s, "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_BUILD_TESTING OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_BUILD_TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_ENABLE_TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_UNIT_TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_BUILD_EXAMPLES OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_ENABLE_EXAMPLES OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_EXAMPLES OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_BUILD_BENCHMARKS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_BENCHMARKS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_BUILD_DOCS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_ENABLE_DOCS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "  set(${_VIX_NS_UPPER}_${_VIX_NAME_UPPER}_DOCS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "endfunction()");
      append_line(s);

      append_line(s, "function(_vix_bridge_alias canonical actual)");
      append_line(s, "  if(TARGET ${canonical})");
      append_line(s, "    return()");
      append_line(s, "  endif()");
      append_line(s, "  if(NOT TARGET ${actual})");
      append_line(s, "    return()");
      append_line(s, "  endif()");
      append_line(s, "  string(REPLACE \"::\" \"__\" _VIX_BRIDGE_SAFE ${canonical})");
      append_line(s, "  set(_VIX_BRIDGE_TARGET \"vix_bridge__${_VIX_BRIDGE_SAFE}\")");
      append_line(s, "  if(NOT TARGET ${_VIX_BRIDGE_TARGET})");
      append_line(s, "    add_library(${_VIX_BRIDGE_TARGET} INTERFACE)");
      append_line(s, "    target_link_libraries(${_VIX_BRIDGE_TARGET} INTERFACE ${actual})");
      append_line(s, "  endif()");
      append_line(s, "  if(NOT TARGET ${canonical})");
      append_line(s, "    add_library(${canonical} ALIAS ${_VIX_BRIDGE_TARGET})");
      append_line(s, "  endif()");
      append_line(s, "endfunction()");
      append_line(s);

      append_line(s, "function(_vix_try_bridge_for_dep dep_ns dep_name)");
      append_line(s, "  set(_VIX_CANONICAL \"${dep_ns}::${dep_name}\")");
      append_line(s, "  if(TARGET ${_VIX_CANONICAL})");
      append_line(s, "    return()");
      append_line(s, "  endif()");
      append_line(s, "  set(_VIX_CANDIDATES");
      append_line(s, "    \"${dep_name}\"");
      append_line(s, "    \"${dep_name}::${dep_name}\"");
      append_line(s, "    \"${dep_ns}_${dep_name}\"");
      append_line(s, "    \"${dep_ns}-${dep_name}\"");
      append_line(s, "    \"${dep_ns}.${dep_name}\"");
      append_line(s, "  )");
      append_line(s, "  foreach(_VIX_CAND IN LISTS _VIX_CANDIDATES)");
      append_line(s, "    if(TARGET ${_VIX_CAND})");
      append_line(s, "      _vix_bridge_alias(${_VIX_CANONICAL} ${_VIX_CAND})");
      append_line(s, "      return()");
      append_line(s, "    endif()");
      append_line(s, "  endforeach()");
      append_line(s, "endfunction()");
      append_line(s);
    }

    void append_global_cmake_defaults(std::string &s)
    {
      append_line(s, "# Disable dependency-side extras to avoid target name collisions");
      append_line(s, "set(BUILD_TESTING OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "set(BUILD_TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "set(ENABLE_TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "set(TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "set(UNIT_TESTS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "set(BUILD_EXAMPLES OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "set(EXAMPLES OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "set(ENABLE_EXAMPLES OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "set(BUILD_BENCHMARKS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "set(BENCHMARKS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "set(BUILD_DOCS OFF CACHE BOOL \"\" FORCE)");
      append_line(s, "set(DOCS OFF CACHE BOOL \"\" FORCE)");
      append_line(s);

      append_line(s, "set(CMAKE_VERBOSE_MAKEFILE ON)");
      append_line(s, "set(CMAKE_RULE_MESSAGES OFF)");
      append_line(s);

      append_line(s, "set(CMAKE_CXX_STANDARD 20)");
      append_line(s, "set(CMAKE_CXX_STANDARD_REQUIRED ON)");
      append_line(s, "set(CMAKE_CXX_EXTENSIONS OFF)");
      append_line(s);

      append_line(s, "# Vix installed package roots");
      append_line(s, "if(DEFINED ENV{VIX_ROOT} AND NOT \"$ENV{VIX_ROOT}\" STREQUAL \"\")");
      append_line(s, "  list(PREPEND CMAKE_PREFIX_PATH \"$ENV{VIX_ROOT}\")");
      append_line(s, "endif()");
      append_line(s);

      append_line(s, "if(DEFINED ENV{HOME} AND NOT \"$ENV{HOME}\" STREQUAL \"\")");
      append_line(s, "  list(APPEND CMAKE_PREFIX_PATH \"$ENV{HOME}/.vix\")");
      append_line(s, "  list(APPEND CMAKE_PREFIX_PATH \"$ENV{HOME}/.local\")");
      append_line(s, "endif()");
      append_line(s);

      append_line(s, "if(DEFINED ENV{USERPROFILE} AND NOT \"$ENV{USERPROFILE}\" STREQUAL \"\")");
      append_line(s, "  list(APPEND CMAKE_PREFIX_PATH \"$ENV{USERPROFILE}/.vix\")");
      append_line(s, "  list(APPEND CMAKE_PREFIX_PATH \"$ENV{USERPROFILE}/.local\")");
      append_line(s, "endif()");
      append_line(s);

      append_line(s, "if (NOT CMAKE_BUILD_TYPE)");
      append_line(s, "  set(CMAKE_BUILD_TYPE Debug CACHE STRING \"Build type\" FORCE)");
      append_line(s, "endif()");
      append_line(s);
    }

    void append_sanitizer_options_block(std::string &s)
    {
      append_line(s, "option(VIX_ENABLE_SANITIZERS \"Enable sanitizers (dev only)\" OFF)");
      append_line(s, "set(VIX_SANITIZER_MODE \"asan_ubsan\" CACHE STRING \"Sanitizer mode: asan, asan_ubsan, ubsan, or tsan\")");
      append_line(s, "set_property(CACHE VIX_SANITIZER_MODE PROPERTY STRINGS asan asan_ubsan ubsan tsan)");
      append_line(s, "option(VIX_ENABLE_LIBCXX_ASSERTS \"Enable libstdc++ debug mode (_GLIBCXX_ASSERTIONS/_GLIBCXX_DEBUG)\" OFF)");
      append_line(s, "option(VIX_ENABLE_HARDENING \"Enable extra hardening flags (non-MSVC)\" OFF)");
      append_line(s);
    }

    void append_dependency_subdirectories(std::string &s, const std::vector<std::pair<std::string, fs::path>> &deps)
    {
      for (const auto &[pkgDir, pkgPath] : deps)
      {
        const std::string depId = dep_dir_to_id(pkgDir);
        const auto slash = depId.find('/');
        const std::string depNs = (slash == std::string::npos) ? depId : depId.substr(0, slash);
        const std::string depName = (slash == std::string::npos) ? depId : depId.substr(slash + 1);

        append_line(s, "_vix_disable_dep_extras(" + depNs + " " + depName + ")");
        append_line(s, "if (EXISTS " + cmake_quote((pkgPath / "CMakeLists.txt").string()) + ")");
        append_line(s, "  add_subdirectory(" + cmake_quote(pkgPath.string()) + " " +
                           cmake_quote_raw("${CMAKE_BINARY_DIR}/_deps/" + pkgDir) +
                           " EXCLUDE_FROM_ALL)");
        append_line(s, "endif()");
        append_line(s, "_vix_try_bridge_for_dep(" + depNs + " " + depName + ")");
      }

      append_line(s);
    }

    void append_target_include_directories(
        std::string &s,
        const std::string &targetName,
        const std::vector<std::string> &dirs,
        bool systemDirs)
    {
      if (dirs.empty())
        return;

      append_line(s, "target_include_directories(" + targetName + (systemDirs ? " SYSTEM PRIVATE" : " PRIVATE"));
      for (const auto &d : dirs)
        append_line(s, "  " + cmake_quote(d));
      append_line(s, ")");
      append_line(s);
    }

    void append_target_compile_definitions(
        std::string &s,
        const std::string &targetName,
        const std::vector<std::string> &defines)
    {
      if (defines.empty())
        return;

      append_line(s, "target_compile_definitions(" + targetName + " PRIVATE");
      for (const auto &d : defines)
        append_line(s, "  " + d);
      append_line(s, ")");
      append_line(s);
    }

    void append_target_compile_options(
        std::string &s,
        const std::string &targetName,
        const std::vector<std::string> &opts)
    {
      if (opts.empty())
        return;

      append_line(s, "target_compile_options(" + targetName + " PRIVATE");
      for (const auto &o : opts)
        append_line(s, "  " + o);
      append_line(s, ")");
      append_line(s);
    }

    void append_explicit_target_options(
        std::string &s,
        const std::string &command,
        const std::string &targetName,
        const std::vector<std::string> &opts)
    {
      if (opts.empty())
        return;
      append_line(s, command + "(" + targetName + " PRIVATE");
      for (const auto &option : opts)
        append_line(s, "  " + cmake_quote(option));
      append_line(s, ")");
      append_line(s);
    }

    void append_target_link_directories(
        std::string &s,
        const std::string &targetName,
        const std::vector<std::string> &dirs)
    {
      if (dirs.empty())
        return;

      append_line(s, "target_link_directories(" + targetName + " PRIVATE");
      for (const auto &d : dirs)
        append_line(s, "  " + cmake_quote(d));
      append_line(s, ")");
      append_line(s);
    }

    void append_target_link_libraries_block(
        std::string &s,
        const std::string &targetName,
        const std::vector<std::string> &libs)
    {
      if (libs.empty())
        return;

      append_line(s, "target_link_libraries(" + targetName + " PRIVATE");
      for (const auto &lib : libs)
        append_line(s, "  " + lib);
      append_line(s, ")");
      append_line(s);
    }

    void append_target_link_options(
        std::string &s,
        const std::string &targetName,
        const std::vector<std::string> &opts)
    {
      if (opts.empty())
        return;

      append_line(s, "target_link_options(" + targetName + " PRIVATE");
      for (const auto &o : opts)
        append_line(s, "  " + o);
      append_line(s, ")");
      append_line(s);
    }

    void append_bridged_dep_links(
        std::string &s,
        const std::string &targetName,
        const std::vector<std::string> &aliases)
    {
      if (aliases.empty())
        return;

      for (const auto &alias : aliases)
      {
        append_line(s, "if (TARGET " + alias + ")");
        append_line(s, "  target_link_libraries(" + targetName + " PRIVATE " + alias + ")");
        append_line(s, "endif()");
      }

      append_line(s);
    }

    void append_vix_deps_link(
        std::string &s,
        const std::string &targetName)
    {
      append_line(s, "if (TARGET vix::deps)");
      append_line(s, "  target_link_libraries(" + targetName + " PRIVATE vix::deps)");
      append_line(s, "endif()");
      append_line(s);
    }

    void append_base_warning_and_platform_flags(std::string &s, const std::string &targetName)
    {
      append_line(s, "if (MSVC)");
      append_line(s, "  target_compile_options(" + targetName + " PRIVATE /W4 /permissive- /EHsc)");
      append_line(s, "  target_compile_definitions(" + targetName + " PRIVATE _CRT_SECURE_NO_WARNINGS)");
      append_line(s, "else()");
      append_line(s, "  target_compile_options(" + targetName + " PRIVATE");
      append_line(s, "    -Wall -Wextra -Wpedantic");
      append_line(s, "    -Wshadow -Wconversion -Wsign-conversion");
      append_line(s, "    -Wformat=2 -Wnull-dereference");
      append_line(s, "  )");
      append_line(s, "endif()");
      append_line(s);

      append_line(s, "if (NOT MSVC)");
      append_line(s, "  target_compile_options(" + targetName + " PRIVATE -fno-omit-frame-pointer)");
      append_line(s, "  if (UNIX AND NOT APPLE)");
      append_line(s, "    target_link_options(" + targetName + " PRIVATE -rdynamic)");
      append_line(s, "  endif()");
      append_line(s, "endif()");
      append_line(s);

      append_line(s, "if (VIX_ENABLE_LIBCXX_ASSERTS AND NOT MSVC)");
      append_line(s, "  target_compile_definitions(" + targetName + " PRIVATE _GLIBCXX_ASSERTIONS)");
      append_line(s, "endif()");
      append_line(s);

      append_line(s, "if (VIX_ENABLE_HARDENING AND NOT MSVC)");
      append_line(s, "  target_compile_options(" + targetName + " PRIVATE -D_FORTIFY_SOURCE=2)");
      append_line(s, "  target_link_options(" + targetName + " PRIVATE -Wl,-z,relro -Wl,-z,now)");
      append_line(s, "endif()");
      append_line(s);
    }

    void append_non_vix_runtime_links(std::string &s, const std::string &targetName)
    {
      append_line(s, "if (UNIX AND NOT APPLE)");
      append_line(s, "  target_link_libraries(" + targetName + " PRIVATE pthread dl)");
      append_line(s, "endif()");
      append_line(s);
    }

    void append_vix_runtime_block(
        std::string &s,
        const std::string &targetName)
    {
      append_line(s, "find_package(Vix CONFIG REQUIRED)");
      append_line(s);

      append_line(s, "set(VIX_MAIN_TARGET \"\")");
      append_line(s, "if (TARGET vix::vix)");
      append_line(s, "  set(VIX_MAIN_TARGET vix::vix)");
      append_line(s, "elseif (TARGET Vix::vix)");
      append_line(s, "  set(VIX_MAIN_TARGET Vix::vix)");
      append_line(s, "else()");
      append_line(s, "  message(FATAL_ERROR \"No umbrella Vix target found (vix::vix or Vix::vix). Runtime scripts require the full Vix package.\")");
      append_line(s, "endif()");
      append_line(s);

      append_line(s, "target_link_libraries(" + targetName + " PRIVATE ${VIX_MAIN_TARGET})");
      append_line(s);
    }

    void append_sanitizer_runtime_block(std::string &s, const std::string &targetName)
    {
      append_line(s, "if (VIX_ENABLE_SANITIZERS AND NOT MSVC)");
      append_line(s, "  if (VIX_SANITIZER_MODE STREQUAL \"tsan\")");
      append_line(s, "    message(STATUS \"Sanitizers: TSan enabled\")");
      append_line(s, "    target_compile_options(" + targetName + " PRIVATE");
      append_line(s, "      -O1 -g3");
      append_line(s, "      -fno-omit-frame-pointer");
      append_line(s, "      -fsanitize=thread");
      append_line(s, "    )");
      append_line(s, "    target_link_options(" + targetName + " PRIVATE -fsanitize=thread)");
      append_line(s, "  elseif (VIX_SANITIZER_MODE STREQUAL \"asan\")");
      append_line(s, "    message(STATUS \"Sanitizers: ASan enabled\")");
      append_line(s, "    target_compile_options(" + targetName + " PRIVATE");
      append_line(s, "      -O1 -g3");
      append_line(s, "      -fno-omit-frame-pointer");
      append_line(s, "      -fsanitize=address");
      append_line(s, "    )");
      append_line(s, "    target_link_options(" + targetName + " PRIVATE -fsanitize=address)");
      append_line(s, "  elseif (VIX_SANITIZER_MODE STREQUAL \"ubsan\")");
      append_line(s, "    message(STATUS \"Sanitizers: UBSan enabled\")");
      append_line(s, "    target_compile_options(" + targetName + " PRIVATE");
      append_line(s, "      -O0 -g3");
      append_line(s, "      -fno-omit-frame-pointer");
      append_line(s, "      -fsanitize=undefined");
      append_line(s, "      -fno-sanitize-recover=all");
      append_line(s, "    )");
      append_line(s, "    target_link_options(" + targetName + " PRIVATE -fsanitize=undefined)");
      append_line(s, "  else()");
      append_line(s, "    message(STATUS \"Sanitizers: ASan+UBSan enabled\")");
      append_line(s, "    target_compile_options(" + targetName + " PRIVATE");
      append_line(s, "      -O1 -g3");
      append_line(s, "      -fno-omit-frame-pointer");
      append_line(s, "      -fsanitize=address,undefined");
      append_line(s, "      -fno-sanitize-recover=all");
      append_line(s, "    )");
      append_line(s, "    target_link_options(" + targetName + " PRIVATE -fsanitize=address,undefined)");
      append_line(s, "    target_compile_definitions(" + targetName + " PRIVATE VIX_ASAN_QUIET=1)");
      append_line(s, "  endif()");
      append_line(s, "endif()");
      append_line(s);

      append_line(s, "if (UNIX AND NOT APPLE)");
      append_line(s, "  target_compile_options(" + targetName + " PRIVATE -g)");
      append_line(s, "endif()");
    }

  } // namespace

  std::string make_script_cmake_target_name(
      const std::string &exeName,
      const fs::path &cppPath)
  {
    return make_unique_script_target_name(
        exeName,
        cppPath);
  }

  fs::path get_scripts_root(bool localCache)
  {
    if (localCache)
      return fs::current_path() / ".vix-scripts";

    if (const auto home = home_dir(); home)
      return fs::path(*home) / ".vix" / "cache" / "scripts";

    return fs::temp_directory_path() / "vix" / "cache" / "scripts";
  }

  std::string make_script_cmakelists(
      const std::string &exeName,
      const fs::path &cppPath,
      bool useVixRuntime,
      const std::vector<std::string> &scriptFlags,
      const std::vector<std::string> &compilerArgs,
      const std::vector<std::string> &linkerArgs)
  {
    std::string s;
    s.reserve(32000);

    const std::string targetName = make_script_cmake_target_name(exeName, cppPath);
    const std::string projectName = make_unique_project_name(exeName, cppPath);
    const std::string outputName = exeName;

    append_line(s, "cmake_minimum_required(VERSION 3.20)");
    append_line(s, "project(" + projectName + " LANGUAGES CXX)");
    append_line(s);

    append_generated_helpers(s);
    append_global_cmake_defaults(s);

    const ScriptFeatures feat = detect_script_features(cppPath);
    const bool effectiveUseVixRuntime = useVixRuntime || feat.usesVix;

    ScriptLinkFlags lf = parse_link_flags(scriptFlags);
    ScriptCompileFlags cf = parse_compile_flags(scriptFlags);

    append_sanitizer_options_block(s);

    const ResolvedScriptDeps deps = resolve_script_deps(cppPath, cf);

    const bool useLocalVixDepsCmake = has_local_vix_deps_cmake(cppPath);

    if (useLocalVixDepsCmake)
    {
      append_local_vix_deps_include(s, cppPath);
    }
    else
    {
      append_dependency_subdirectories(s, deps.uniqueCmakeDeps);
    }

    append_line(s, "add_executable(" + targetName + " " + cmake_quote(cppPath.string()) + ")");
    append_line(s, "set_target_properties(" + targetName + " PROPERTIES OUTPUT_NAME " + cmake_quote(outputName) + ")");
    append_line(s);

    append_target_include_directories(s, targetName, cf.includeDirs, false);
    append_target_include_directories(s, targetName, cf.systemIncludeDirs, true);

    std::vector<std::string> scriptDepAliases = deps.cmakeDepAliases;

    if (useLocalVixDepsCmake)
    {
      const fs::path projectRoot = find_script_project_root(cppPath);
      const fs::path lockPath = projectRoot / "vix.lock";

      scriptDepAliases = lock_package_ids_to_aliases(
          load_ordered_packages_from_lock(lockPath));
    }

    append_bridged_dep_links(s, targetName, scriptDepAliases);

    if (useLocalVixDepsCmake)
    {
      append_vix_deps_link(s, targetName);
    }

    append_target_compile_definitions(s, targetName, cf.defines);
    append_target_compile_options(s, targetName, cf.compileOpts);
    append_explicit_target_options(s, "target_compile_options", targetName, compilerArgs);
    append_target_link_directories(s, targetName, lf.libDirs);
    append_target_link_libraries_block(s, targetName, lf.libs);
    append_target_link_options(s, targetName, lf.linkOpts);
    append_explicit_target_options(s, "target_link_options", targetName, linkerArgs);
    append_base_warning_and_platform_flags(s, targetName);

    if (!effectiveUseVixRuntime)
    {
      append_non_vix_runtime_links(s, targetName);
    }
    else
    {
      append_vix_runtime_block(s, targetName);
    }

    append_sanitizer_runtime_block(s, targetName);

    append_line(s, "message(STATUS \"Vix script fallback mode: external non-Vix libraries are not auto-guessed\")");
    append_line(s, "message(STATUS \"If link fails, pass an explicit linker argument, for example: vix run file.cpp --linker-arg=-lfmt\")");

    return s;
  }

} // namespace vix::commands::RunCommand::detail
