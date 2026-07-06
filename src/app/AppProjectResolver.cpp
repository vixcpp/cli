/**
 *
 *  @file AppProjectResolver.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Project resolver for CMake and vix.app based applications.
 *
 */

#include <vix/cli/app/AppProjectResolver.hpp>

#include <vix/cli/app/AppCMakeGenerator.hpp>
#include <vix/cli/app/AppManifest.hpp>

#include <vix/cli/util/Lockfile.hpp>
#include <vix/cli/util/Manifest.hpp>
#include <vix/cli/util/Resolver.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace vix::cli::app
{
  namespace
  {
    static bool file_exists_regular(const fs::path &path)
    {
      std::error_code ec;
      return fs::exists(path, ec) && fs::is_regular_file(path, ec);
    }

    static fs::path normalize_absolute(const fs::path &path)
    {
      std::error_code ec;

      fs::path out = fs::absolute(path, ec);

      if (ec)
        out = path;

      return out.lexically_normal();
    }

    static fs::path app_manifest_json_path(const fs::path &projectDir)
    {
      return projectDir / "vix.json";
    }

    static fs::path app_lock_path(const fs::path &projectDir)
    {
      return projectDir / "vix.lock";
    }

    static bool parse_registry_dep_spec(
        const std::string &raw,
        std::string &packageId,
        std::string &version)
    {
      std::string value = raw;

      while (!value.empty() &&
             std::isspace(static_cast<unsigned char>(value.front())) != 0)
      {
        value.erase(value.begin());
      }

      while (!value.empty() &&
             std::isspace(static_cast<unsigned char>(value.back())) != 0)
      {
        value.pop_back();
      }

      if (value.empty())
        return false;

      if (value.front() == '@')
        value.erase(value.begin());

      const std::size_t slash = value.find('/');

      if (slash == std::string::npos || slash == 0 || slash + 1 >= value.size())
        return false;

      const std::size_t atVersion = value.find('@', slash + 1);

      if (atVersion == std::string::npos)
      {
        packageId = value;
        version.clear();
        return true;
      }

      packageId = value.substr(0, atVersion);
      version = value.substr(atVersion + 1);

      return !packageId.empty() && !version.empty();
    }

    static std::string trim_copy_local(std::string value)
    {
      auto is_space = [](unsigned char c)
      {
        return std::isspace(c) != 0;
      };

      while (!value.empty() && is_space(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());

      while (!value.empty() && is_space(static_cast<unsigned char>(value.back())))
        value.pop_back();

      return value;
    }

    static std::string strip_quotes_local(const std::string &value)
    {
      const std::string s = trim_copy_local(value);

      if (s.size() >= 2 &&
          ((s.front() == '"' && s.back() == '"') ||
           (s.front() == '\'' && s.back() == '\'')))
      {
        return s.substr(1, s.size() - 2);
      }

      return s;
    }

    static std::vector<std::string> parse_vix_module_array(
        const fs::path &path,
        const std::string &section,
        const std::string &key)
    {
      std::vector<std::string> out;

      std::ifstream in(path);
      if (!in)
        return out;

      std::string activeSection;
      bool collecting = false;
      std::string line;

      while (std::getline(in, line))
      {
        std::string s = trim_copy_local(line);

        const std::size_t comment = s.find('#');
        if (comment != std::string::npos)
          s = trim_copy_local(s.substr(0, comment));

        if (s.empty())
          continue;

        if (!collecting && s.size() >= 2 && s.front() == '[' && s.back() == ']')
        {
          activeSection = trim_copy_local(s.substr(1, s.size() - 2));
          continue;
        }

        if (activeSection != section)
          continue;

        if (!collecting)
        {
          const std::size_t eq = s.find('=');
          if (eq == std::string::npos)
            continue;

          const std::string currentKey =
              trim_copy_local(s.substr(0, eq));

          if (currentKey != key)
            continue;

          std::string value = trim_copy_local(s.substr(eq + 1));

          if (value.find('[') == std::string::npos)
            continue;

          collecting = true;

          const std::size_t open = value.find('[');
          value = value.substr(open + 1);

          const std::size_t close = value.find(']');
          if (close != std::string::npos)
          {
            value = value.substr(0, close);
            collecting = false;
          }

          std::stringstream ss(value);
          std::string item;

          while (std::getline(ss, item, ','))
          {
            item = strip_quotes_local(trim_copy_local(item));
            if (!item.empty())
              out.push_back(item);
          }

          continue;
        }

        const std::size_t close = s.find(']');
        if (close != std::string::npos)
        {
          s = s.substr(0, close);
          collecting = false;
        }

        std::stringstream ss(s);
        std::string item;

        while (std::getline(ss, item, ','))
        {
          item = strip_quotes_local(trim_copy_local(item));
          if (!item.empty())
            out.push_back(item);
        }
      }

      return out;
    }

    static fs::path module_manifest_path(
        const fs::path &projectDir,
        const AppModule &module)
    {
      const std::string name = module.name;

      const std::string path =
          module.path.empty() ? ("modules/" + name) : module.path;

      return (projectDir / path / "vix.module").lexically_normal();
    }

    static bool contains_string(
        const std::vector<std::string> &values,
        const std::string &needle)
    {
      return std::find(values.begin(), values.end(), needle) != values.end();
    }

    static void merge_enabled_module_registry_deps(
        AppManifest &manifest,
        const fs::path &projectDir)
    {
      for (const AppModule &module : manifest.appModules)
      {
        if (!module.enabled)
          continue;

        const fs::path path =
            module_manifest_path(projectDir, module);

        const std::vector<std::string> deps =
            parse_vix_module_array(path, "deps", "registry");

        for (const std::string &dep : deps)
        {
          if (!contains_string(manifest.deps, dep))
            manifest.deps.push_back(dep);
        }
      }
    }

    static bool sync_vix_app_registry_deps(
        const AppManifest &manifest,
        const fs::path &projectDir,
        std::string &error)
    {
      if (manifest.deps.empty())
        return true;

      const fs::path manifestPath = app_manifest_json_path(projectDir);
      const fs::path lockPath = app_lock_path(projectDir);

      for (const std::string &dep : manifest.deps)
      {
        std::string packageId;
        std::string version;

        if (!parse_registry_dep_spec(dep, packageId, version))
        {
          error = "Invalid vix.app dependency: " + dep;
          return false;
        }

        const std::string requested =
            version.empty() ? std::string("*") : version;

        try
        {
          vix::cli::util::manifest::upsert_manifest_dependency_or_throw(
              manifestPath,
              vix::cli::util::manifest::Dependency{
                  packageId,
                  requested});
        }
        catch (const std::exception &ex)
        {
          error = std::string("Failed to update vix.json from vix.app deps: ") + ex.what();
          return false;
        }
      }

      try
      {
        const auto manifestDependencies =
            vix::cli::util::manifest::read_manifest_dependencies_or_throw(
                manifestPath);

        const auto lockedDependencies =
            vix::cli::util::resolver::resolve_project_dependencies_or_throw(
                manifestDependencies);

        vix::cli::util::lockfile::write_lockfile_replace_all_or_throw(
            lockPath,
            lockedDependencies);
      }
      catch (const std::exception &ex)
      {
        error = std::string("Failed to resolve vix.app dependencies: ") + ex.what();
        return false;
      }

      return true;
    }

    static fs::path search_project_root(const fs::path &base)
    {
      fs::path current = normalize_absolute(base);

      if (file_exists_regular(current))
        current = current.parent_path();

      while (!current.empty())
      {
        if (file_exists_regular(current / "CMakeLists.txt") ||
            file_exists_regular(current / "vix.app"))
        {
          return current;
        }

        const fs::path parent = current.parent_path();

        if (parent == current)
          break;

        current = parent;
      }

      return {};
    }

    static AppProjectResolveResult resolve_cmake_project(
        const fs::path &projectDir)
    {
      AppProjectResolveResult result;

      result.kind = AppProjectKind::CMake;
      result.userProjectDir = projectDir;
      result.cmakeSourceDir = projectDir;
      result.cmakeListsPath = projectDir / "CMakeLists.txt";
      result.targetName = projectDir.filename().string();
      result.generated = false;

      return result;
    }

    static AppProjectResolveResult resolve_vix_app_project(
        const fs::path &projectDir)
    {
      AppProjectResolveResult result;

      result.kind = AppProjectKind::VixApp;
      result.userProjectDir = projectDir;
      result.appManifestPath = projectDir / "vix.app";
      result.generated = true;

      const AppManifestLoadResult loadResult =
          load_app_manifest(result.appManifestPath);

      if (!loadResult.success())
      {
        result.error = loadResult.error;
        return result;
      }

      AppManifest manifest = loadResult.manifest;

      merge_enabled_module_registry_deps(
          manifest,
          projectDir);

      std::string depsError;

      if (!sync_vix_app_registry_deps(
              manifest,
              projectDir,
              depsError))
      {
        result.error = depsError;
        return result;
      }

      const AppCMakeGenerateResult generateResult =
          generate_app_cmake_project(
              manifest,
              projectDir);

      if (!generateResult.success())
      {
        result.error = generateResult.error;
        return result;
      }

      result.cmakeSourceDir = generateResult.sourceDir;
      result.cmakeListsPath = generateResult.cmakeListsPath;
      result.targetName = manifest.name;

      return result;
    }
  } // namespace

  std::string to_string(AppProjectKind kind)
  {
    switch (kind)
    {
    case AppProjectKind::CMake:
      return "cmake";
    case AppProjectKind::VixApp:
      return "vix.app";
    case AppProjectKind::Unknown:
    default:
      return "unknown";
    }
  }

  bool AppProjectResolveResult::success() const
  {
    return error.empty() &&
           kind != AppProjectKind::Unknown &&
           !userProjectDir.empty() &&
           !cmakeSourceDir.empty() &&
           !cmakeListsPath.empty();
  }

  AppProjectResolveResult resolve_app_project(const fs::path &base)
  {
    AppProjectResolveResult result;

    const fs::path projectDir = search_project_root(base);

    if (projectDir.empty())
    {
      result.error =
          "Unable to determine the project directory. Missing CMakeLists.txt or vix.app.";
      return result;
    }

    const fs::path cmakeListsPath = projectDir / "CMakeLists.txt";
    const fs::path appManifestPath = projectDir / "vix.app";

    if (file_exists_regular(cmakeListsPath))
      return resolve_cmake_project(projectDir);

    if (file_exists_regular(appManifestPath))
      return resolve_vix_app_project(projectDir);

    result.error =
        "Unable to determine the project directory. Missing CMakeLists.txt or vix.app.";

    return result;
  }

} // namespace vix::cli::app
