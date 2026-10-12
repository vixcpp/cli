/**
 * @file CMakeProjectAdapter.hpp
 * @brief CMake configured-metadata to normalized-project adapter.
 */
#ifndef VIX_CLI_PROJECT_CMAKE_CMAKE_PROJECT_ADAPTER_HPP
#define VIX_CLI_PROJECT_CMAKE_CMAKE_PROJECT_ADAPTER_HPP

#include <optional>

#include <vix/cli/project/ProjectLocation.hpp>
#include <vix/cli/project/ProjectSnapshot.hpp>
#include <vix/cli/project/cmake/CMakeFileApi.hpp>

namespace vix::cli::project::cmake
{
  struct SnapshotRequest
  {
    Path buildRoot;
    std::optional<std::string> configuration;
  };

  struct ProjectSnapshotResult
  {
    std::optional<ProjectSnapshot> snapshot;
    std::optional<ProjectError> error;
    [[nodiscard]] bool success() const noexcept { return snapshot.has_value(); }
  };

  struct NativeBuildTargetResult
  {
    std::optional<std::string> target;
    std::optional<ProjectError> error;
    [[nodiscard]] bool success() const noexcept { return target.has_value(); }
  };

  struct RunnableArtifactResult
  {
    std::optional<Path> artifact;
    std::optional<ProjectError> error;
    [[nodiscard]] bool success() const noexcept { return artifact.has_value(); }
  };

  [[nodiscard]] TargetKind target_kind_from_cmake_type(const std::string &type) noexcept;

  /** Maps already-read configured CMake metadata into a normalized snapshot. */
  [[nodiscard]] ProjectSnapshotResult make_project_snapshot(
      const ProjectLocation &location,
      const SnapshotRequest &request,
      const CMakeFileApiCodemodel &codemodel);

  /** Loads a configured CMake snapshot.  This function never runs CMake. */
  [[nodiscard]] ProjectSnapshotResult load_cmake_project_snapshot(
      const ProjectLocation &location,
      const SnapshotRequest &request);

  /** Resolves an opaque normalized ID through configured CMake metadata. */
  [[nodiscard]] NativeBuildTargetResult resolve_native_build_target(
      const ProjectLocation &location,
      const SnapshotRequest &request,
      const TargetId &id);

  /** Resolves CMake's configured primary executable artifact for an opaque ID. */
  [[nodiscard]] RunnableArtifactResult resolve_runnable_artifact(
      const ProjectLocation &location,
      const SnapshotRequest &request,
      const TargetId &id);
} // namespace vix::cli::project::cmake

#endif // VIX_CLI_PROJECT_CMAKE_CMAKE_PROJECT_ADAPTER_HPP
