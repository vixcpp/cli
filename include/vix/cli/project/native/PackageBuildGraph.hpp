/** @file PackageBuildGraph.hpp
 *  Deterministic CMake target graph for a locked native Vix project.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_PACKAGE_BUILD_GRAPH_HPP
#define VIX_CLI_PROJECT_NATIVE_PACKAGE_BUILD_GRAPH_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <vix/cli/project/native/Models.hpp>
#include <vix/cli/project/native/PackageContentStore.hpp>
#include <vix/engine/SanitizerMode.hpp>

namespace vix::cli::project::native
{
  struct MaterializedLockedPackage
  {
    std::string nodeId;
    PackageContentId content;
    std::filesystem::path root;
  };

  enum class PackageBuildGraphIssueCode
  {
    invalid_project,
    invalid_lockfile,
    missing_materialization,
    unexpected_materialization,
    missing_package_metadata,
    invalid_package_contract,
    invalid_native_package,
    target_collision,
    missing_declared_target,
    invalid_package_dependency,
    filesystem_failure
  };

  struct PackageBuildGraphIssue
  {
    PackageBuildGraphIssueCode code{PackageBuildGraphIssueCode::invalid_project};
    std::string subject;
    std::string message;
  };

  struct PackageBuildGraphResult
  {
    std::optional<std::string> cmakeLists;
    std::vector<PackageBuildGraphIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return cmakeLists.has_value() && issues.empty(); }
  };

  // Emits CMake source for an already-locked, already-materialized graph. It
  // performs no acquisition, resolution, writes, or build execution. The
  // caller owns the separate generated build/source directory policy.
  [[nodiscard]] PackageBuildGraphResult generate_package_build_graph(
    const NativeProject &project,
    const std::filesystem::path &projectRoot,
    const Lockfile &lockfile,
    const std::vector<MaterializedLockedPackage> &packages,
    vix::engine::SanitizerMode diagnosticMode = vix::engine::SanitizerMode::None);
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_PACKAGE_BUILD_GRAPH_HPP
