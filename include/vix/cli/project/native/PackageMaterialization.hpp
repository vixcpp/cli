/** @file PackageMaterialization.hpp
 *  Deterministic materialization of verified P10P package content.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_PACKAGE_MATERIALIZATION_HPP
#define VIX_CLI_PROJECT_NATIVE_PACKAGE_MATERIALIZATION_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <vix/cli/project/native/FilesystemPackageContentStore.hpp>

namespace vix::cli::project::native
{
  enum class PackageMaterializationState { materialized, reused };

  enum class PackageMaterializationErrorCode
  {
    content_store_failure,
    invalid_content,
    invalid_destination,
    destination_conflict,
    filesystem_failure
  };

  struct PackageMaterializationIssue
  {
    PackageMaterializationErrorCode code{PackageMaterializationErrorCode::filesystem_failure};
    std::string message;
    std::vector<PackageContentIssue> contentIssues;
  };

  struct PackageMaterializationResult
  {
    std::optional<std::filesystem::path> path;
    std::optional<PackageMaterializationState> state;
    std::vector<PackageMaterializationIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return path.has_value() && state.has_value() && issues.empty(); }
  };

  // Materializes only the verified canonical object read from P10P. The
  // injected root owns placement policy; the content ID alone selects a path.
  [[nodiscard]] PackageMaterializationResult materialize_package_content(
    const PackageContentId &id,
    const FilesystemPackageContentStore &store,
    const std::filesystem::path &destinationRoot);
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_PACKAGE_MATERIALIZATION_HPP
