/** @file MetadataAcquisition.hpp
 *  Synchronous metadata observation and transitive catalog acquisition.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_METADATA_ACQUISITION_HPP
#define VIX_CLI_PROJECT_NATIVE_METADATA_ACQUISITION_HPP

#include <optional>
#include <string>
#include <vector>

#include <vix/cli/project/native/ResolutionCatalog.hpp>
#include <vix/cli/project/native/AcquisitionMode.hpp>

namespace vix::cli::project::native
{
  class OfflineCachedPackageMetadataSource;

  struct RegistryQueryResult
  {
    bool succeeded{true};
    std::vector<RegistryCandidate> releases;
    std::string diagnostic;
  };

  enum class GitQueryStatus { found, not_found, failed };

  struct GitQueryResult
  {
    GitQueryStatus status{GitQueryStatus::not_found};
    std::optional<GitResolution> resolution;
    std::string diagnostic;
  };

  // Semantic metadata boundary only. Implementations do not expose transport,
  // Git, filesystem, cache, or package-content operations here. A future
  // metadata cache (versions/contracts/revision observations) is distinct from
  // the later store of immutable package bytes; neither is implemented here.
  class PackageMetadataSource
  {
  public:
    virtual ~PackageMetadataSource() = default;
    [[nodiscard]] virtual RegistryQueryResult registry_releases(
      const RegistryPackageIdentity &identity) = 0;
    [[nodiscard]] virtual GitQueryResult resolve_git(
      const GitPackageIdentity &identity,
      const GitRevisionRequirement &revision) = 0;
  };

  enum class MetadataAcquisitionErrorCode
  {
    registry_source_failure,
    git_source_failure,
    catalog_construction_failure,
    invalid_source_response
  };

  struct MetadataAcquisitionIssue
  {
    MetadataAcquisitionErrorCode code{MetadataAcquisitionErrorCode::invalid_source_response};
    std::string subject;
    std::string message;
    std::optional<ResolutionCatalogErrorCode> catalogCode;
  };

  struct MetadataAcquisitionResult
  {
    std::optional<ResolutionCatalog> catalog;
    std::vector<MetadataAcquisitionIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return catalog.has_value() && issues.empty(); }
  };

  // Observes all transitive metadata reachable from root declarations. Registry
  // queries return complete observed release sets; no requirement filtering or
  // package-content acquisition occurs in this layer.
  [[nodiscard]] MetadataAcquisitionResult acquire_resolution_catalog(
    const std::vector<PackageRequirement> &rootRequirements,
    PackageMetadataSource &source);

  // Operation-policy entry point. Online uses the supplied existing online
  // source path; offline is restricted to P10L's cached/trusted adapter.
  [[nodiscard]] MetadataAcquisitionResult acquire_resolution_catalog(
    const std::vector<PackageRequirement> &rootRequirements,
    AcquisitionMode mode,
    PackageMetadataSource &onlineSource,
    OfflineCachedPackageMetadataSource &offlineSource);
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_METADATA_ACQUISITION_HPP
