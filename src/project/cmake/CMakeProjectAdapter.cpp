/** @file CMakeProjectAdapter.cpp */
#include <vix/cli/project/cmake/CMakeProjectAdapter.hpp>

#include <algorithm>

namespace vix::cli::project::cmake
{
  namespace
  {
    [[nodiscard]] Path normalize(const Path &path)
    {
      std::error_code error;
      Path absolute = std::filesystem::absolute(path, error);
      return (error ? path : absolute).lexically_normal();
    }
  }

  TargetKind target_kind_from_cmake_type(const std::string &type) noexcept
  {
    if (type == "EXECUTABLE") return TargetKind::executable;
    if (type == "STATIC_LIBRARY") return TargetKind::static_library;
    if (type == "SHARED_LIBRARY") return TargetKind::shared_library;
    if (type == "MODULE_LIBRARY") return TargetKind::module_library;
    if (type == "INTERFACE_LIBRARY") return TargetKind::interface_library;
    if (type == "OBJECT_LIBRARY") return TargetKind::object_library;
    if (type == "UTILITY") return TargetKind::utility;
    return TargetKind::unknown;
  }

  ProjectSnapshotResult make_project_snapshot(
      const ProjectLocation &location,
      const SnapshotRequest &request,
      const CMakeFileApiCodemodel &codemodel)
  {
    if (location.system != ProjectSystem::cmake ||
        normalize(location.root) != codemodel.sourceRoot ||
        normalize(request.buildRoot) != codemodel.buildRoot)
      return {std::nullopt, ProjectError::configured_metadata_unavailable};

    const CMakeFileApiConfiguration *selected = nullptr;
    if (codemodel.configurations.size() == 1)
      selected = &codemodel.configurations.front();
    else if (!request.configuration)
      return {std::nullopt, ProjectError::configuration_required};
    else
    {
      for (const auto &configuration : codemodel.configurations)
      {
        if (configuration.name == *request.configuration)
        {
          selected = &configuration;
          break;
        }
      }
      if (selected == nullptr)
        return {std::nullopt, ProjectError::configuration_not_found};
    }

    ProjectSnapshot snapshot;
    snapshot.project.root = normalize(location.root);
    snapshot.project.name = codemodel.projectName;
    snapshot.context.buildRoot = codemodel.buildRoot;
    snapshot.context.configuration = selected->name;

    for (const CMakeFileApiTarget &native : selected->targets)
    {
      if (native.generatorProvided || native.imported)
        continue;

      Target target;
      target.id.value = native.id;
      target.name = native.name;
      target.kind = target_kind_from_cmake_type(native.type);
      target.capabilities.buildable = !native.abstract;
      target.capabilities.runnable =
          target.kind == TargetKind::executable && !native.artifacts.empty();
      snapshot.project.targets.push_back(std::move(target));

      for (const Path &artifact : native.artifacts)
        snapshot.artifacts.push_back({TargetId{native.id}, artifact});
    }

    std::sort(snapshot.project.targets.begin(), snapshot.project.targets.end(), [](const Target &left, const Target &right)
    {
      return left.name == right.name ? left.id.value < right.id.value : left.name < right.name;
    });
    std::sort(snapshot.artifacts.begin(), snapshot.artifacts.end(), [](const TargetArtifact &left, const TargetArtifact &right)
    {
      return left.target.value == right.target.value
          ? left.path.generic_string() < right.path.generic_string()
          : left.target.value < right.target.value;
    });
    return {std::move(snapshot), std::nullopt};
  }

  ProjectSnapshotResult load_cmake_project_snapshot(
      const ProjectLocation &location,
      const SnapshotRequest &request)
  {
    const auto codemodel = load_cmake_file_api_codemodel(request.buildRoot);
    if (!codemodel.success())
      return {std::nullopt, codemodel.error};
    return make_project_snapshot(location, request, *codemodel.codemodel);
  }
} // namespace vix::cli::project::cmake
