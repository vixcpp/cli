/** @file ResolutionCatalog.hpp
 *  Immutable in-memory metadata snapshot for deterministic package resolution.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_RESOLUTION_CATALOG_HPP
#define VIX_CLI_PROJECT_NATIVE_RESOLUTION_CATALOG_HPP

#include <optional>
#include <string>
#include <vector>

#include <vix/cli/project/native/Resolver.hpp>

namespace vix::cli::project::native
{
  enum class ResolutionCatalogErrorCode
  {
    invalid_contract,
    registry_contract_identity_mismatch,
    registry_contract_version_mismatch,
    invalid_git_contract_source,
    invalid_repository_identity,
    invalid_content_digest,
    duplicate_registry_release,
    conflicting_registry_release,
    duplicate_git_revision,
    conflicting_git_revision,
    conflicting_git_commit_metadata
  };

  struct ResolutionCatalogIssue
  {
    ResolutionCatalogErrorCode code{ResolutionCatalogErrorCode::invalid_contract};
    std::string subject;
    std::string message;
  };

  struct ResolutionCatalogBuildResult;

  class ResolutionCatalog final : public ResolutionMetadataProvider
  {
  public:
    [[nodiscard]] std::vector<RegistryCandidate> registry_candidates(
      const RegistryPackageIdentity &identity) const override;
    [[nodiscard]] std::vector<GitResolution> git_resolutions(
      const std::string &normalizedRepository,
      const GitRevisionRequirement &revision) const override;

  private:
    friend struct ResolutionCatalogBuildResult;
    friend ResolutionCatalogBuildResult build_resolution_catalog(
      std::vector<RegistryCandidate>, std::vector<GitResolution>);

    ResolutionCatalog(std::vector<RegistryCandidate> registry, std::vector<GitResolution> git);

    std::vector<RegistryCandidate> registry_;
    std::vector<GitResolution> git_;
  };

  struct ResolutionCatalogBuildResult
  {
    std::optional<ResolutionCatalog> value;
    std::vector<ResolutionCatalogIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return value.has_value() && issues.empty(); }
  };

  // Builds a finalized value snapshot. Duplicate immutable keys and conflicting
  // Git commit metadata are errors; insertion order never selects a winner.
  [[nodiscard]] ResolutionCatalogBuildResult build_resolution_catalog(
    std::vector<RegistryCandidate> registry,
    std::vector<GitResolution> git);
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_RESOLUTION_CATALOG_HPP
