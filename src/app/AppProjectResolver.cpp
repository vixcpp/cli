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
#include <vix/cli/modules/ModuleGraph.hpp>
#include <vix/cli/modules/DependencyOwnership.hpp>
#include <vix/cli/modules/DependencyConstraints.hpp>

#include <vix/cli/util/Lockfile.hpp>
#include <vix/cli/util/Manifest.hpp>
#include <vix/cli/util/Resolver.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
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

    static bool analyze_active_registry_constraints(
        const vix::cli::modules::DependencyOwnership &ownership,
        std::vector<std::string> &requirements,
        std::string &error)
    {
      std::map<std::string, std::vector<std::string>> available;
      for (const auto &requirement : ownership.requirements)
      {
        if (requirement.source != vix::cli::modules::DependencySource::Registry ||
            !requirement.owner.active)
          continue;
        const auto identity = vix::cli::modules::registry_dependency_identity(
            requirement.requirement);
        if (!identity.has_value()) { error = "Invalid registry requirement: " + requirement.requirement; return false; }
        if (available.find(*identity) == available.end())
        {
          try { available.emplace(*identity, vix::cli::util::resolver::available_registry_versions_or_throw(*identity)); }
          catch (const std::exception &ex) { error = ex.what(); return false; }
        }
      }
      const auto analysis = vix::cli::modules::analyze_dependency_constraints(ownership, available);
      if (!analysis.error.empty()) { error = analysis.error; return false; }
      if (!analysis.conflicts.empty())
      {
        const auto &conflict = analysis.conflicts.front();
        error = "Dependency conflict: " + conflict.packageId + ". " + conflict.reason;
        return false;
      }
      requirements.clear();
      for (const auto &item : analysis.resolvedRegistry)
        requirements.push_back(item.packageId + "@" + item.version);
      return true;
    }

    static bool sync_vix_app_registry_deps(
        const std::vector<std::string> &requirements,
        const fs::path &projectDir,
        std::string &error)
    {
      if (requirements.empty())
        return true;

      const fs::path manifestPath = app_manifest_json_path(projectDir);
      const fs::path lockPath = app_lock_path(projectDir);

      for (const std::string &dep : requirements)
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

      std::string graphError;
      const auto graph = vix::cli::modules::ModuleGraph::from_app_modules(
          manifest.appModules, graphError);
      if (!graph.valid() || !graph.validate_paths(projectDir, true, graphError))
      {
        result.error = "Invalid module graph: " + graphError;
        return result;
      }

      const auto ownership = vix::cli::modules::build_dependency_ownership(
          manifest, graph, projectDir);
      if (!ownership.success())
      {
        result.error = "Invalid dependency ownership: " + ownership.error;
        return result;
      }
      const auto gitAnalysis = vix::cli::modules::analyze_owned_git_constraints(ownership);
      if (!gitAnalysis.success())
      {
        const auto &conflict = gitAnalysis.conflicts.front();
        result.error = "Git dependency conflict: " + conflict.repository + ". " + conflict.reason;
        return result;
      }
      std::vector<std::string> requirements;
      if (!analyze_active_registry_constraints(ownership, requirements, graphError))
      {
        result.error = graphError;
        return result;
      }

      std::string depsError;

      if (!sync_vix_app_registry_deps(
              requirements,
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
