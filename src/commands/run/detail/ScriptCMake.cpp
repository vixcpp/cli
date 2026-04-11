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
#include <vix/utils/Env.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vix::commands::RunCommand::detail
{
  namespace
  {
    struct ScriptFeatures
    {
      bool usesVix = false;
      bool usesOrm = false;
      bool usesDb = false;
      bool usesMysql = false;
    };

    struct GlobalPackage
    {
      std::string id;
      std::string pkgDir;
      std::string includeDir{"include"};
      std::string type{"header-only"};
      fs::path installedPath;
    };

    struct ScriptCompileFlags
    {
      std::vector<std::string> includeDirs;
      std::vector<std::string> systemDirs;
      std::vector<std::string> defines;
      std::vector<std::string> compileOpts;
    };

    struct ResolvedScriptDeps
    {
      std::vector<std::string> orderedDepIds;
      std::vector<std::pair<std::string, fs::path>> uniqueCmakeDeps;
      std::vector<std::string> cmakeDepAliases;
      std::vector<std::string> includeDirs;
    };

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

    std::optional<std::string> home_dir()
    {
#ifdef _WIN32
      const char *home = vix::utils::vix_getenv("USERPROFILE");
#else
      const char *home = vix::utils::vix_getenv("HOME");
#endif
      if (!home || std::string(home).empty())
        return std::nullopt;

      return std::string(home);
    }

    fs::path vix_root()
    {
      if (const auto home = home_dir(); home)
        return fs::path(*home) / ".vix";

      return fs::path(".vix");
    }

    fs::path global_manifest_path()
    {
      return vix_root() / "global" / "installed.json";
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

    std::vector<std::string> load_package_dependencies_from_manifest(const fs::path &pkgPath)
    {
      std::vector<std::string> deps;

      const fs::path manifestPath = pkgPath / "vix.json";
      if (!fs::exists(manifestPath))
        return deps;

      std::ifstream ifs(manifestPath);
      if (!ifs)
        return deps;

      nlohmann::json j;
      ifs >> j;

      auto read_dep_block = [&](const nlohmann::json &d)
      {
        if (d.is_object())
        {
          for (auto it = d.begin(); it != d.end(); ++it)
            deps.push_back(it.key());

          return;
        }

        if (d.is_array())
        {
          for (const auto &item : d)
          {
            if (item.is_string())
            {
              deps.push_back(item.get<std::string>());
            }
            else if (item.is_object())
            {
              if (item.contains("id") && item["id"].is_string())
                deps.push_back(item["id"].get<std::string>());
              else if (item.contains("name") && item["name"].is_string())
                deps.push_back(item["name"].get<std::string>());
              else if (item.contains("package") && item["package"].is_string())
                deps.push_back(item["package"].get<std::string>());
            }
          }
        }
      };

      if (j.contains("dependencies"))
        read_dep_block(j["dependencies"]);

      if (deps.empty() && j.contains("deps"))
        read_dep_block(j["deps"]);

      return deps;
    }

    std::vector<std::pair<std::string, std::string>> topo_sort_dep_packages(
        const std::vector<std::pair<std::string, std::string>> &depPackages)
    {
      std::vector<std::pair<std::string, std::string>> ordered;

      std::unordered_map<std::string, std::string> dirToPath;
      std::unordered_map<std::string, std::vector<std::string>> graph;
      std::unordered_map<std::string, int> indegree;

      for (const auto &[pkgDir, pkgPath] : depPackages)
      {
        dirToPath[pkgDir] = pkgPath;
        graph[pkgDir] = {};
        indegree[pkgDir] = 0;
      }

      for (const auto &[pkgDir, pkgPath] : depPackages)
      {
        const auto manifestDeps = load_package_dependencies_from_manifest(pkgPath);

        for (const auto &depId : manifestDeps)
        {
          const std::string depDir = dep_id_to_dir(depId);
          if (!dirToPath.contains(depDir))
            continue;

          graph[depDir].push_back(pkgDir);
          indegree[pkgDir]++;
        }
      }

      std::vector<std::string> zero;
      for (const auto &[pkgDir, _] : depPackages)
      {
        if (indegree[pkgDir] == 0)
          zero.push_back(pkgDir);
      }

      std::sort(zero.begin(), zero.end());

      std::queue<std::string> q;
      for (const auto &pkgDir : zero)
        q.push(pkgDir);

      while (!q.empty())
      {
        const std::string cur = q.front();
        q.pop();

        ordered.push_back({cur, dirToPath[cur]});

        auto nexts = graph[cur];
        std::sort(nexts.begin(), nexts.end());

        for (const auto &next : nexts)
        {
          indegree[next]--;
          if (indegree[next] == 0)
            q.push(next);
        }
      }

      if (ordered.size() != depPackages.size())
        return depPackages;

      return ordered;
    }

    ScriptFeatures detect_script_features(const fs::path &cppPath)
    {
      ScriptFeatures f;

      std::ifstream ifs(cppPath);
      if (!ifs)
        return f;

      std::string line;
      while (std::getline(ifs, line))
      {
        auto has = [&](const char *s)
        { return line.find(s) != std::string::npos; };

        if (has("vix::") || has("Vix::") || has("#include <vix/") || has("#include \"vix/"))
          f.usesVix = true;

        if (has("#include <vix/orm/") || has("vix::orm") || has("using namespace vix::orm"))
          f.usesOrm = true;

        if (has("#include <vix/db/") || has("vix::db") || has("using namespace vix::db"))
          f.usesDb = true;

        if (has("make_mysql_factory") || has("Engine::MySQL") || has("mysqlcppconn"))
          f.usesMysql = true;

        if (!f.usesMysql && (has("tcp://") || has("3306") || has("MySQL")))
          f.usesMysql = true;
      }

      if (f.usesOrm)
        f.usesDb = true;

      return f;
    }

    bool starts_with(const std::string &s, const char *p)
    {
      return s.rfind(p, 0) == 0;
    }

    ScriptCompileFlags parse_compile_flags(const std::vector<std::string> &flags)
    {
      ScriptCompileFlags out;

      for (std::size_t i = 0; i < flags.size(); ++i)
      {
        const std::string &f = flags[i];

        if (starts_with(f, "-I") && f.size() > 2)
        {
          out.includeDirs.push_back(f.substr(2));
          continue;
        }
        if (f == "-I" && i + 1 < flags.size())
        {
          out.includeDirs.push_back(flags[++i]);
          continue;
        }

        if (starts_with(f, "-isystem") && f.size() > 8)
        {
          out.systemDirs.push_back(f.substr(8));
          continue;
        }
        if (f == "-isystem" && i + 1 < flags.size())
        {
          out.systemDirs.push_back(flags[++i]);
          continue;
        }

        if (starts_with(f, "-D") && f.size() > 2)
        {
          out.defines.push_back(f.substr(2));
          continue;
        }
        if (f == "-D" && i + 1 < flags.size())
        {
          out.defines.push_back(flags[++i]);
          continue;
        }

        if (!f.empty() && f[0] == '-')
          out.compileOpts.push_back(f);
      }

      return out;
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

        if (j.contains("dependencies") && j["dependencies"].is_object())
        {
          for (auto it = j["dependencies"].begin(); it != j["dependencies"].end(); ++it)
            ordered.push_back(it.key());
          return ordered;
        }
      }

      return ordered;
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

    std::vector<std::string> extract_script_include_prefixes(const fs::path &cppPath)
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

        std::string inc = trim_copy(line.substr(start + 1, end - start - 1));
        if (inc.empty())
          continue;

        const auto slash = inc.find('/');
        const std::string prefix = (slash == std::string::npos) ? inc : inc.substr(0, slash);
        if (prefix.empty())
          continue;

        prefixes.push_back(prefix);
      }

      std::sort(prefixes.begin(), prefixes.end());
      prefixes.erase(std::unique(prefixes.begin(), prefixes.end()), prefixes.end());
      return prefixes;
    }

    std::vector<GlobalPackage> load_global_packages()
    {
      std::vector<GlobalPackage> out;

      const fs::path manifestPath = global_manifest_path();
      if (!fs::exists(manifestPath))
        return out;

      std::ifstream ifs(manifestPath);
      if (!ifs)
        return out;

      nlohmann::json root;
      ifs >> root;

      if (!root.is_object() || !root.contains("packages") || !root["packages"].is_array())
        return out;

      for (const auto &item : root["packages"])
      {
        if (!item.is_object())
          continue;

        if (!item.contains("id") || !item["id"].is_string())
          continue;

        if (!item.contains("installed_path") || !item["installed_path"].is_string())
          continue;

        GlobalPackage pkg;
        pkg.id = item["id"].get<std::string>();
        pkg.pkgDir = dep_id_to_dir(pkg.id);
        pkg.installedPath = fs::path(item["installed_path"].get<std::string>());

        if (item.contains("include") && item["include"].is_string())
          pkg.includeDir = item["include"].get<std::string>();

        if (item.contains("type") && item["type"].is_string())
          pkg.type = item["type"].get<std::string>();

        out.push_back(std::move(pkg));
      }

      return out;
    }

    bool package_matches_script_includes(
        const GlobalPackage &pkg,
        const std::vector<std::string> &includePrefixes)
    {
      const fs::path includeRoot = pkg.installedPath / pkg.includeDir;
      if (!fs::exists(includeRoot) || !fs::is_directory(includeRoot))
        return false;

      for (const auto &prefix : includePrefixes)
      {
        if (fs::exists(includeRoot / prefix))
          return true;
      }

      return false;
    }

    std::vector<GlobalPackage> select_global_packages_for_script(
        const fs::path &cppPath,
        const std::vector<std::string> &localPackageIds)
    {
      std::vector<GlobalPackage> selected;
      const auto allGlobals = load_global_packages();
      const auto includePrefixes = extract_script_include_prefixes(cppPath);

      if (allGlobals.empty() || includePrefixes.empty())
        return selected;

      std::unordered_set<std::string> localIds(localPackageIds.begin(), localPackageIds.end());
      std::unordered_map<std::string, GlobalPackage> globalsById;

      for (const auto &pkg : allGlobals)
        globalsById[pkg.id] = pkg;

      std::unordered_set<std::string> visited;

      std::function<void(const GlobalPackage &)> visit = [&](const GlobalPackage &pkg)
      {
        if (pkg.id.empty())
          return;

        if (localIds.contains(pkg.id))
          return;

        if (!visited.insert(pkg.id).second)
          return;

        selected.push_back(pkg);

        for (const auto &depId : load_package_dependencies_from_manifest(pkg.installedPath))
        {
          if (localIds.contains(depId))
            continue;

          const auto it = globalsById.find(depId);
          if (it != globalsById.end())
            visit(it->second);
        }
      };

      for (const auto &pkg : allGlobals)
      {
        if (localIds.contains(pkg.id))
          continue;

        if (package_matches_script_includes(pkg, includePrefixes))
          visit(pkg);
      }

      return selected;
    }

    ResolvedScriptDeps resolve_script_deps(
        const fs::path &cppPath,
        ScriptCompileFlags &cf)
    {
      ResolvedScriptDeps resolved;

      const fs::path depsRoot = cppPath.parent_path() / ".vix" / "deps";
      const fs::path lockPath = cppPath.parent_path() / "vix.lock";

      resolved.orderedDepIds = load_ordered_packages_from_lock(lockPath);

      if (resolved.orderedDepIds.empty())
      {
        const auto depPackages = extract_dep_packages_from_include_dirs(cf.includeDirs);
        for (const auto &[pkgDir, _pkgPath] : depPackages)
          resolved.orderedDepIds.push_back(pkgDir);
      }

      std::unordered_set<std::string> localPackageIds;
      for (const auto &depId : resolved.orderedDepIds)
      {
        if (depId.find('/') != std::string::npos || depId.find('@') != std::string::npos)
          localPackageIds.insert(depId);
        else
          localPackageIds.insert(dep_dir_to_id(depId));
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

      const std::vector<std::string> localIdsForGlobal(localPackageIds.begin(), localPackageIds.end());
      const auto globalPkgs = select_global_packages_for_script(cppPath, localIdsForGlobal);

      for (const auto &pkg : globalPkgs)
      {
        const fs::path includePath = pkg.installedPath / pkg.includeDir;
        if (fs::exists(includePath))
          append_unique_string(cf.includeDirs, includePath.string());

        if (fs::exists(pkg.installedPath / "CMakeLists.txt"))
          cmakeDeps.emplace_back(pkg.pkgDir, pkg.installedPath);
      }

      std::vector<std::pair<std::string, std::string>> topoInput;
      topoInput.reserve(cmakeDeps.size());

      for (const auto &[pkgDir, pkgPath] : cmakeDeps)
        topoInput.emplace_back(pkgDir, pkgPath.string());

      const auto topoSorted = topo_sort_dep_packages(topoInput);

      std::vector<std::pair<std::string, fs::path>> cmakeDepsSorted;
      for (const auto &[pkgDir, pkgPath] : topoSorted)
        cmakeDepsSorted.emplace_back(pkgDir, fs::path(pkgPath));

      std::unordered_set<std::string> seenPkgDirs;
      for (const auto &[pkgDir, pkgPath] : cmakeDepsSorted)
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

      append_line(s, "if (NOT CMAKE_BUILD_TYPE)");
      append_line(s, "  set(CMAKE_BUILD_TYPE Debug CACHE STRING \"Build type\" FORCE)");
      append_line(s, "endif()");
      append_line(s);
    }

    void append_sanitizer_options_block(std::string &s, const ScriptFeatures &feat)
    {
      append_line(s, "option(VIX_ENABLE_SANITIZERS \"Enable sanitizers (dev only)\" OFF)");
      append_line(s, "set(VIX_SANITIZER_MODE \"asan_ubsan\" CACHE STRING \"Sanitizer mode: asan_ubsan or ubsan\")");
      append_line(s, "set_property(CACHE VIX_SANITIZER_MODE PROPERTY STRINGS asan_ubsan ubsan)");
      append_line(s, "option(VIX_ENABLE_LIBCXX_ASSERTS \"Enable libstdc++ debug mode (_GLIBCXX_ASSERTIONS/_GLIBCXX_DEBUG)\" OFF)");
      append_line(s, "option(VIX_ENABLE_HARDENING \"Enable extra hardening flags (non-MSVC)\" OFF)");
      append_line(s, std::string("option(VIX_USE_ORM \"Enable Vix ORM (requires vix::orm)\" ") + (feat.usesOrm ? "ON" : "OFF") + ")");
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
        const std::string &targetName,
        bool withSqlite,
        bool withMySql,
        bool wantsOrm,
        bool wantsMysqlConnectorHint)
    {
      if (withSqlite || withMySql)
        append_line(s, "set(VIX_ENABLE_DB ON CACHE BOOL \"\" FORCE)");

      if (withSqlite)
      {
        append_line(s, "set(VIX_DB_USE_SQLITE ON CACHE BOOL \"\" FORCE)");
        append_line(s, "set(VIX_DB_REQUIRE_SQLITE ON CACHE BOOL \"\" FORCE)");
      }

      if (withMySql)
      {
        append_line(s, "set(VIX_DB_USE_MYSQL ON CACHE BOOL \"\" FORCE)");
        append_line(s, "set(VIX_DB_REQUIRE_MYSQL ON CACHE BOOL \"\" FORCE)");
      }

      if (withSqlite || withMySql)
        append_line(s);

      append_line(s, "find_package(vix QUIET CONFIG)");
      append_line(s, "if (NOT vix_FOUND)");
      append_line(s, "  find_package(Vix CONFIG REQUIRED)");
      append_line(s, "endif()");
      append_line(s);

      append_line(s, "set(VIX_MAIN_TARGET \"\")");
      append_line(s, "if (TARGET vix::vix)");
      append_line(s, "  set(VIX_MAIN_TARGET vix::vix)");
      append_line(s, "elseif (TARGET Vix::vix)");
      append_line(s, "  set(VIX_MAIN_TARGET Vix::vix)");
      append_line(s, "elseif (TARGET vix::core)");
      append_line(s, "  set(VIX_MAIN_TARGET vix::core)");
      append_line(s, "elseif (TARGET Vix::core)");
      append_line(s, "  set(VIX_MAIN_TARGET Vix::core)");
      append_line(s, "else()");
      append_line(s, "  message(FATAL_ERROR \"No Vix target found (vix::vix/Vix::vix/vix::core/Vix::core)\")");
      append_line(s, "endif()");
      append_line(s);

      append_line(s, "target_link_libraries(" + targetName + " PRIVATE ${VIX_MAIN_TARGET})");
      append_line(s);

      if (wantsOrm)
      {
        append_line(s, "if (VIX_USE_ORM)");
        append_line(s, "  if (TARGET vix::orm)");
        append_line(s, "    target_link_libraries(" + targetName + " PRIVATE vix::orm)");
        append_line(s, "    target_compile_definitions(" + targetName + " PRIVATE VIX_USE_ORM=1)");
        append_line(s, "  elseif (TARGET Vix::orm)");
        append_line(s, "    target_link_libraries(" + targetName + " PRIVATE Vix::orm)");
        append_line(s, "    target_compile_definitions(" + targetName + " PRIVATE VIX_USE_ORM=1)");
        append_line(s, "  else()");
        append_line(s, "    message(FATAL_ERROR \"VIX_USE_ORM=ON but ORM target is not available (vix::orm/Vix::orm)\")");
        append_line(s, "  endif()");
        append_line(s);
        append_line(s, "  if (TARGET vix::db)");
        append_line(s, "    target_link_libraries(" + targetName + " PRIVATE vix::db)");
        append_line(s, "  elseif (TARGET Vix::db)");
        append_line(s, "    target_link_libraries(" + targetName + " PRIVATE Vix::db)");
        append_line(s, "  else()");
        append_line(s, "    message(FATAL_ERROR \"VIX_USE_ORM=ON but DB target is not available (vix::db/Vix::db)\")");
        append_line(s, "  endif()");
        append_line(s, "endif()");
        append_line(s);
      }

      if (wantsMysqlConnectorHint)
      {
        append_line(s, "if (UNIX)");
        append_line(s, "  find_library(VIX_MYSQLCPPCONN_LIB NAMES mysqlcppconn8 mysqlcppconn)");
        append_line(s, "  if (VIX_MYSQLCPPCONN_LIB)");
        append_line(s, "    target_link_libraries(" + targetName + " PRIVATE ${VIX_MYSQLCPPCONN_LIB})");
        append_line(s, "  else()");
        append_line(s, "    message(WARNING \"MySQL connector lib not found (mysqlcppconn8/mysqlcppconn). If you get undefined references, pass: -- -lmysqlcppconn8\")");
        append_line(s, "  endif()");
        append_line(s, "endif()");
        append_line(s);
      }
    }

    void append_sanitizer_runtime_block(std::string &s, const std::string &targetName)
    {
      append_line(s, "if (VIX_ENABLE_SANITIZERS AND NOT MSVC)");
      append_line(s, "  if (VIX_SANITIZER_MODE STREQUAL \"ubsan\")");
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

    bool has_link_library(const ScriptLinkFlags &lf, const std::string &name)
    {
      return std::find(lf.libs.begin(), lf.libs.end(), name) != lf.libs.end();
    }

  } // namespace

  ScriptLinkFlags parse_link_flags(const std::vector<std::string> &flags)
  {
    ScriptLinkFlags out;

    for (const auto &f : flags)
    {
      if (f.rfind("-l", 0) == 0 && f.size() > 2)
      {
        out.libs.push_back(f.substr(2));
        continue;
      }

      if (f.rfind("-L", 0) == 0 && f.size() > 2)
      {
        out.libDirs.push_back(f.substr(2));
        continue;
      }

      if (f.rfind("-I", 0) == 0 || f == "-I" ||
          f.rfind("-D", 0) == 0 || f == "-D" ||
          f.rfind("-isystem", 0) == 0 || f == "-isystem")
      {
        continue;
      }

      out.linkOpts.push_back(f);
    }

    return out;
  }

  bool script_uses_vix(const fs::path &cppPath)
  {
    std::ifstream ifs(cppPath);
    if (!ifs)
      return false;

    std::string line;
    while (std::getline(ifs, line))
    {
      if (line.find("vix::") != std::string::npos ||
          line.find("Vix::") != std::string::npos)
      {
        return true;
      }

      if (line.find("#include") == std::string::npos)
        continue;

      if (line.find("vix") != std::string::npos ||
          line.find("Vix") != std::string::npos)
      {
        return true;
      }
    }

    return false;
  }

  fs::path get_scripts_root()
  {
    return fs::current_path() / ".vix-scripts";
  }

  std::string make_script_cmakelists(
      const std::string &exeName,
      const fs::path &cppPath,
      bool useVixRuntime,
      const std::vector<std::string> &scriptFlags,
      bool withSqlite,
      bool withMySql)
  {
    std::string s;
    s.reserve(32000);

    const std::string targetName = make_unique_script_target_name(exeName, cppPath);
    const std::string projectName = make_unique_project_name(exeName, cppPath);
    const std::string outputName = exeName;

    append_line(s, "cmake_minimum_required(VERSION 3.20)");
    append_line(s, "project(" + projectName + " LANGUAGES CXX)");
    append_line(s);

    append_generated_helpers(s);
    append_global_cmake_defaults(s);

    const ScriptFeatures feat = detect_script_features(cppPath);
    ScriptLinkFlags lf = parse_link_flags(scriptFlags);
    ScriptCompileFlags cf = parse_compile_flags(scriptFlags);

    append_sanitizer_options_block(s, feat);

    const ResolvedScriptDeps deps = resolve_script_deps(cppPath, cf);
    append_dependency_subdirectories(s, deps.uniqueCmakeDeps);

    append_line(s, "add_executable(" + targetName + " " + cmake_quote(cppPath.string()) + ")");
    append_line(s, "set_target_properties(" + targetName + " PROPERTIES OUTPUT_NAME " + cmake_quote(outputName) + ")");
    append_line(s);

    append_target_include_directories(s, targetName, cf.includeDirs, false);
    append_target_include_directories(s, targetName, cf.systemDirs, true);
    append_bridged_dep_links(s, targetName, deps.cmakeDepAliases);
    append_target_compile_definitions(s, targetName, cf.defines);
    append_target_compile_options(s, targetName, cf.compileOpts);
    append_target_link_directories(s, targetName, lf.libDirs);
    append_target_link_libraries_block(s, targetName, lf.libs);
    append_target_link_options(s, targetName, lf.linkOpts);
    append_base_warning_and_platform_flags(s, targetName);

    if (!useVixRuntime)
    {
      append_non_vix_runtime_links(s, targetName);
    }
    else
    {
      const bool needsMysqlConnectorHint =
          feat.usesMysql &&
          !has_link_library(lf, "mysqlcppconn8") &&
          !has_link_library(lf, "mysqlcppconn");

      append_vix_runtime_block(
          s,
          targetName,
          withSqlite,
          withMySql,
          feat.usesOrm,
          needsMysqlConnectorHint);
    }

    append_sanitizer_runtime_block(s, targetName);
    return s;
  }

} // namespace vix::commands::RunCommand::detail
