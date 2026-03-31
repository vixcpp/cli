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

#include <fstream>
#include <string>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <optional>
#include <filesystem>
#include <cstdlib>
#include <cctype>

namespace vix::commands::RunCommand::detail
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

  static std::string trim_copy(std::string s)
  {
    auto is_ws = [](unsigned char c)
    { return std::isspace(c) != 0; };

    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front())))
      s.erase(s.begin());

    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back())))
      s.pop_back();

    return s;
  }

  static std::string dep_dir_to_id(const std::string &pkgDir)
  {
    std::string id = pkgDir;
    std::replace(id.begin(), id.end(), '.', '/');
    return id;
  }

  static std::string dep_id_to_dir(std::string depId)
  {
    depId.erase(std::remove(depId.begin(), depId.end(), '@'), depId.end());
    std::replace(depId.begin(), depId.end(), '/', '.');
    return depId;
  }

  static std::optional<std::string> home_dir()
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

  static fs::path vix_root()
  {
    if (const auto home = home_dir(); home)
      return fs::path(*home) / ".vix";
    return fs::path(".vix");
  }

  static fs::path global_manifest_path()
  {
    return vix_root() / "global" / "installed.json";
  }

  static std::vector<std::string> load_package_dependencies_from_manifest(const fs::path &pkgPath)
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

    if (!j.contains("dependencies"))
      return deps;

    const auto &d = j["dependencies"];

    if (d.is_object())
    {
      for (auto it = d.begin(); it != d.end(); ++it)
        deps.push_back(it.key());
    }
    else if (d.is_array())
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

    return deps;
  }

  static std::vector<std::pair<std::string, std::string>> topo_sort_dep_packages(
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

    std::queue<std::string> q;

    std::vector<std::string> zero;
    for (const auto &[pkgDir, _] : depPackages)
    {
      if (indegree[pkgDir] == 0)
        zero.push_back(pkgDir);
    }

    std::sort(zero.begin(), zero.end());
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

  static ScriptFeatures detect_script_features(const fs::path &cppPath)
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

      if (f.rfind("-I", 0) == 0 || f == "-I" || f.rfind("-D", 0) == 0 || f == "-D" ||
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
    auto cwd = fs::current_path();
    return cwd / ".vix-scripts";
  }

  struct ScriptCompileFlags
  {
    std::vector<std::string> includeDirs;
    std::vector<std::string> systemDirs;
    std::vector<std::string> defines;
    std::vector<std::string> compileOpts;
  };

  static inline bool starts_with(const std::string &s, const char *p)
  {
    return s.rfind(p, 0) == 0;
  }

  static ScriptCompileFlags parse_compile_flags(const std::vector<std::string> &flags)
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

  static std::string dep_id_to_cmake_alias(const std::string &id)
  {
    std::string alias = id;
    std::replace(alias.begin(), alias.end(), '/', ':');
    const auto pos = alias.find(':');
    if (pos != std::string::npos)
      alias.replace(pos, 1, "::");
    return alias;
  }

  static std::vector<std::string> load_ordered_packages_from_lock(const fs::path &lockPath)
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

  static std::vector<std::pair<std::string, std::string>> extract_dep_packages_from_include_dirs(
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
      std::string tail = d.substr(pos + marker.size());

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

  static std::vector<std::string> extract_script_include_prefixes(const fs::path &cppPath)
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

  static std::vector<GlobalPackage> load_global_packages()
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

  static bool package_matches_script_includes(
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

  static std::vector<GlobalPackage> select_global_packages_for_script(
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

  std::string make_script_cmakelists(
      const std::string &exeName,
      const fs::path &cppPath,
      bool useVixRuntime,
      const std::vector<std::string> &scriptFlags)
  {
    std::string s;
    s.reserve(10000);

    auto q = [](const std::string &p)
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
    };

    auto q_raw = [](const std::string &p)
    {
      return "\"" + p + "\"";
    };

    s += "cmake_minimum_required(VERSION 3.20)\n";
    s += "project(" + exeName + " LANGUAGES CXX)\n\n";

    const fs::path scriptDir = cppPath.parent_path() / ".vix-scripts" / exeName;
    const fs::path depsFile = scriptDir / "vix_deps.cmake";

    s += "if (EXISTS " + q(depsFile.string()) + ")\n";
    s += "  include(" + q(depsFile.string()) + ")\n";
    s += "endif()\n\n";

    s += "set(CMAKE_VERBOSE_MAKEFILE ON)\n";
    s += "set(CMAKE_RULE_MESSAGES OFF)\n\n";

    s += "set(CMAKE_CXX_STANDARD 20)\n";
    s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    s += "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

    s += "if (NOT CMAKE_BUILD_TYPE)\n";
    s += "  set(CMAKE_BUILD_TYPE Debug CACHE STRING \"Build type\" FORCE)\n";
    s += "endif()\n\n";

    auto feat = detect_script_features(cppPath);

    s += "option(VIX_ENABLE_SANITIZERS \"Enable sanitizers (dev only)\" OFF)\n";
    s += "set(VIX_SANITIZER_MODE \"asan_ubsan\" CACHE STRING \"Sanitizer mode: asan_ubsan or ubsan\")\n";
    s += "set_property(CACHE VIX_SANITIZER_MODE PROPERTY STRINGS asan_ubsan ubsan)\n";
    s += "option(VIX_ENABLE_LIBCXX_ASSERTS \"Enable libstdc++ debug mode (_GLIBCXX_ASSERTIONS/_GLIBCXX_DEBUG)\" OFF)\n";
    s += "option(VIX_ENABLE_HARDENING \"Enable extra hardening flags (non-MSVC)\" OFF)\n";
    s += std::string("option(VIX_USE_ORM \"Enable Vix ORM (requires vix::orm)\" ") + (feat.usesOrm ? "ON" : "OFF") + ")\n";

    auto lf = parse_link_flags(scriptFlags);
    auto cf = parse_compile_flags(scriptFlags);

    const fs::path lockPath = cppPath.parent_path() / "vix.lock";
    auto orderedDeps = load_ordered_packages_from_lock(lockPath);

    const fs::path depsRoot = cppPath.parent_path() / ".vix" / "deps";

    if (orderedDeps.empty())
    {
      const auto depPackages = extract_dep_packages_from_include_dirs(cf.includeDirs);
      for (const auto &[pkgDir, _pkgPath] : depPackages)
        orderedDeps.push_back(pkgDir);
    }

    std::unordered_set<std::string> localPackageIds;
    for (const auto &depId : orderedDeps)
    {
      if (depId.find('/') != std::string::npos || depId.find('@') != std::string::npos)
        localPackageIds.insert(depId);
      else
        localPackageIds.insert(dep_dir_to_id(depId));
    }

    std::vector<std::pair<std::string, fs::path>> cmakeDeps;
    cmakeDeps.reserve(orderedDeps.size());

    for (const auto &depId : orderedDeps)
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
        cmakeDeps.emplace_back(pkgDir, pkgPath);
    }

    const auto globalPkgs = select_global_packages_for_script(cppPath, orderedDeps);

    for (const auto &pkg : globalPkgs)
    {
      const fs::path includePath = pkg.installedPath / pkg.includeDir;
      if (fs::exists(includePath))
      {
        const std::string includeStr = includePath.string();
        if (std::find(cf.includeDirs.begin(), cf.includeDirs.end(), includeStr) == cf.includeDirs.end())
          cf.includeDirs.push_back(includeStr);
      }

      if (fs::exists(pkg.installedPath / "CMakeLists.txt"))
        cmakeDeps.emplace_back(pkg.pkgDir, pkg.installedPath);
    }

    std::vector<std::pair<std::string, std::string>> topoInput;
    topoInput.reserve(cmakeDeps.size());
    for (const auto &[pkgDir, pkgPath] : cmakeDeps)
      topoInput.emplace_back(pkgDir, pkgPath.string());

    const auto topoSorted = topo_sort_dep_packages(topoInput);

    std::vector<std::pair<std::string, fs::path>> cmakeDepsSorted;
    cmakeDepsSorted.reserve(topoSorted.size());
    for (const auto &[pkgDir, pkgPath] : topoSorted)
      cmakeDepsSorted.emplace_back(pkgDir, fs::path(pkgPath));

    std::unordered_set<std::string> seenPkgDirs;
    std::vector<std::pair<std::string, fs::path>> uniqueCmakeDeps;
    uniqueCmakeDeps.reserve(cmakeDepsSorted.size());

    for (const auto &[pkgDir, pkgPath] : cmakeDepsSorted)
    {
      if (seenPkgDirs.insert(pkgDir).second)
        uniqueCmakeDeps.emplace_back(pkgDir, pkgPath);
    }

    std::vector<std::string> cmakeDepAliases;
    cmakeDepAliases.reserve(uniqueCmakeDeps.size());

    for (const auto &[pkgDir, _pkgPath] : uniqueCmakeDeps)
      cmakeDepAliases.push_back(dep_id_to_cmake_alias(dep_dir_to_id(pkgDir)));

    std::sort(cmakeDepAliases.begin(), cmakeDepAliases.end());
    cmakeDepAliases.erase(
        std::unique(cmakeDepAliases.begin(), cmakeDepAliases.end()),
        cmakeDepAliases.end());

    auto rank_dep = [](const std::string &pkgDir) -> int
    {
      if (pkgDir == "softadastra.core")
        return 0;
      if (pkgDir == "softadastra.fs")
        return 1;
      if (pkgDir == "softadastra.wal")
        return 2;
      if (pkgDir == "softadastra.store")
        return 3;
      if (pkgDir == "softadastra.sync")
        return 4;
      if (pkgDir == "softadastra.transport")
        return 5;
      return 100;
    };

    std::stable_sort(uniqueCmakeDeps.begin(), uniqueCmakeDeps.end(),
                     [&](const auto &a, const auto &b)
                     {
                       return rank_dep(a.first) < rank_dep(b.first);
                     });

    for (const auto &[pkgDir, pkgPath] : uniqueCmakeDeps)
    {
      s += "if (EXISTS " + q((pkgPath / "CMakeLists.txt").string()) + ")\n";
      s += "  add_subdirectory(" + q(pkgPath.string()) + " " +
           q_raw("${CMAKE_BINARY_DIR}/_deps/" + pkgDir) +
           " EXCLUDE_FROM_ALL)\n";
      s += "endif()\n";
    }

    s += "\n";
    s += "add_executable(" + exeName + " " + q(cppPath.string()) + ")\n\n";

    if (!cf.includeDirs.empty() || !cf.systemDirs.empty())
    {
      s += "target_include_directories(" + exeName + " PRIVATE\n";
      for (const auto &d : cf.includeDirs)
        s += "  " + q(d) + "\n";
      s += ")\n\n";

      if (!cf.systemDirs.empty())
      {
        s += "target_include_directories(" + exeName + " SYSTEM PRIVATE\n";
        for (const auto &d : cf.systemDirs)
          s += "  " + q(d) + "\n";
        s += ")\n\n";
      }
    }

    if (!cmakeDepAliases.empty())
    {
      for (const auto &alias : cmakeDepAliases)
      {
        s += "if (TARGET " + alias + ")\n";
        s += "  target_link_libraries(" + exeName + " PRIVATE " + alias + ")\n";
        s += "endif()\n";
      }
      s += "\n";
    }

    if (!cf.defines.empty())
    {
      s += "target_compile_definitions(" + exeName + " PRIVATE\n";
      for (const auto &d : cf.defines)
        s += "  " + d + "\n";
      s += ")\n\n";
    }

    if (!cf.compileOpts.empty())
    {
      s += "target_compile_options(" + exeName + " PRIVATE\n";
      for (const auto &o : cf.compileOpts)
        s += "  " + o + "\n";
      s += ")\n\n";
    }

    auto hasLib = [&](const std::string &name) -> bool
    {
      return std::find(lf.libs.begin(), lf.libs.end(), name) != lf.libs.end();
    };

    if (!lf.libDirs.empty())
    {
      s += "target_link_directories(" + exeName + " PRIVATE\n";
      for (const auto &d : lf.libDirs)
        s += "  " + q(d) + "\n";
      s += ")\n\n";
    }

    if (!lf.libs.empty())
    {
      s += "target_link_libraries(" + exeName + " PRIVATE\n";
      for (const auto &L : lf.libs)
        s += "  " + L + "\n";
      s += ")\n\n";
    }

    if (!lf.linkOpts.empty())
    {
      s += "target_link_options(" + exeName + " PRIVATE\n";
      for (const auto &o : lf.linkOpts)
        s += "  " + o + "\n";
      s += ")\n\n";
    }

    s += "if (MSVC)\n";
    s += "  target_compile_options(" + exeName + " PRIVATE /W4 /permissive- /EHsc)\n";
    s += "  target_compile_definitions(" + exeName + " PRIVATE _CRT_SECURE_NO_WARNINGS)\n";
    s += "else()\n";
    s += "  target_compile_options(" + exeName + " PRIVATE\n";
    s += "    -Wall -Wextra -Wpedantic\n";
    s += "    -Wshadow -Wconversion -Wsign-conversion\n";
    s += "    -Wformat=2 -Wnull-dereference\n";
    s += "  )\n";
    s += "endif()\n\n";

    s += "if (NOT MSVC)\n";
    s += "  target_compile_options(" + exeName + " PRIVATE -fno-omit-frame-pointer)\n";
    s += "  if (UNIX AND NOT APPLE)\n";
    s += "    target_link_options(" + exeName + " PRIVATE -rdynamic)\n";
    s += "  endif()\n";
    s += "endif()\n\n";

    s += "if (VIX_ENABLE_LIBCXX_ASSERTS AND NOT MSVC)\n";
    s += "  target_compile_definitions(" + exeName + " PRIVATE _GLIBCXX_ASSERTIONS)\n";
    s += "endif()\n\n";

    s += "if (VIX_ENABLE_HARDENING AND NOT MSVC)\n";
    s += "  target_compile_options(" + exeName + " PRIVATE -D_FORTIFY_SOURCE=2)\n";
    s += "  target_link_options(" + exeName + " PRIVATE -Wl,-z,relro -Wl,-z,now)\n";
    s += "endif()\n\n";

    if (!useVixRuntime)
    {
      s += "if (UNIX AND NOT APPLE)\n";
      s += "  target_link_libraries(" + exeName + " PRIVATE pthread dl)\n";
      s += "endif()\n\n";
    }
    else
    {
      s += "# Prefer lowercase package, fallback to legacy Vix\n";
      s += "find_package(vix QUIET CONFIG)\n";
      s += "if (NOT vix_FOUND)\n";
      s += "  find_package(Vix CONFIG REQUIRED)\n";
      s += "endif()\n\n";

      s += "# Pick main target (umbrella preferred)\n";
      s += "set(VIX_MAIN_TARGET \"\")\n";
      s += "if (TARGET vix::vix)\n";
      s += "  set(VIX_MAIN_TARGET vix::vix)\n";
      s += "elseif (TARGET Vix::vix)\n";
      s += "  set(VIX_MAIN_TARGET Vix::vix)\n";
      s += "elseif (TARGET vix::core)\n";
      s += "  set(VIX_MAIN_TARGET vix::core)\n";
      s += "elseif (TARGET Vix::core)\n";
      s += "  set(VIX_MAIN_TARGET Vix::core)\n";
      s += "else()\n";
      s += "  message(FATAL_ERROR \"No Vix target found (vix::vix/Vix::vix/vix::core/Vix::core)\")\n";
      s += "endif()\n\n";

      s += "target_link_libraries(" + exeName + " PRIVATE ${VIX_MAIN_TARGET})\n\n";

      s += "if (VIX_USE_ORM)\n";
      s += "  if (TARGET vix::orm)\n";
      s += "    target_link_libraries(" + exeName + " PRIVATE vix::orm)\n";
      s += "    target_compile_definitions(" + exeName + " PRIVATE VIX_USE_ORM=1)\n";
      s += "  elseif (TARGET Vix::orm)\n";
      s += "    target_link_libraries(" + exeName + " PRIVATE Vix::orm)\n";
      s += "    target_compile_definitions(" + exeName + " PRIVATE VIX_USE_ORM=1)\n";
      s += "  else()\n";
      s += "    message(FATAL_ERROR \"VIX_USE_ORM=ON but ORM target is not available (vix::orm/Vix::orm)\")\n";
      s += "  endif()\n";
      s += "\n";
      s += "  if (TARGET vix::db)\n";
      s += "    target_link_libraries(" + exeName + " PRIVATE vix::db)\n";
      s += "  elseif (TARGET Vix::db)\n";
      s += "    target_link_libraries(" + exeName + " PRIVATE Vix::db)\n";
      s += "  else()\n";
      s += "    message(FATAL_ERROR \"VIX_USE_ORM=ON but DB target is not available (vix::db/Vix::db)\")\n";
      s += "  endif()\n";
      s += "endif()\n\n";

      if (feat.usesMysql && !hasLib("mysqlcppconn8") && !hasLib("mysqlcppconn"))
      {
        s += "# Auto-link MySQL connector when script uses MySQL\n";
        s += "if (UNIX)\n";
        s += "  find_library(VIX_MYSQLCPPCONN_LIB NAMES mysqlcppconn8 mysqlcppconn)\n";
        s += "  if (VIX_MYSQLCPPCONN_LIB)\n";
        s += "    target_link_libraries(" + exeName + " PRIVATE ${VIX_MYSQLCPPCONN_LIB})\n";
        s += "  else()\n";
        s += "    message(WARNING \"MySQL connector lib not found (mysqlcppconn8/mysqlcppconn). If you get undefined references, pass: -- -lmysqlcppconn8\")\n";
        s += "  endif()\n";
        s += "endif()\n\n";
      }
    }

    s += "if (VIX_ENABLE_SANITIZERS AND NOT MSVC)\n";
    s += "  if (VIX_SANITIZER_MODE STREQUAL \"ubsan\")\n";
    s += "    message(STATUS \"Sanitizers: UBSan enabled\")\n";
    s += "    target_compile_options(" + exeName + " PRIVATE\n";
    s += "      -O0 -g3\n";
    s += "      -fno-omit-frame-pointer\n";
    s += "      -fsanitize=undefined\n";
    s += "      -fno-sanitize-recover=all\n";
    s += "    )\n";
    s += "    target_link_options(" + exeName + " PRIVATE -fsanitize=undefined)\n";
    s += "  else()\n";
    s += "    message(STATUS \"Sanitizers: ASan+UBSan enabled\")\n";
    s += "    target_compile_options(" + exeName + " PRIVATE\n";
    s += "      -O1 -g3\n";
    s += "      -fno-omit-frame-pointer\n";
    s += "      -fsanitize=address,undefined\n";
    s += "      -fno-sanitize-recover=all\n";
    s += "    )\n";
    s += "    target_link_options(" + exeName + " PRIVATE -fsanitize=address,undefined)\n";
    s += "    target_compile_definitions(" + exeName + " PRIVATE VIX_ASAN_QUIET=1)\n";
    s += "  endif()\n";
    s += "endif()\n\n";

    s += "if (UNIX AND NOT APPLE)\n";
    s += "  target_compile_options(" + exeName + " PRIVATE -g)\n";
    s += "endif()\n";

    return s;
  }
}
