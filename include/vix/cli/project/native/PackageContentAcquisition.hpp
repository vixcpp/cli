/** @file PackageContentAcquisition.hpp
 *  Exact-lock package content acquisition orchestration (P10Q-A).
 */
#ifndef VIX_CLI_PROJECT_NATIVE_PACKAGE_CONTENT_ACQUISITION_HPP
#define VIX_CLI_PROJECT_NATIVE_PACKAGE_CONTENT_ACQUISITION_HPP

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <vix/cli/project/native/PackageContentDigest.hpp>
#include <vix/cli/project/native/RegistryDirectory.hpp>

namespace vix::cli::project::native
{
  struct RegistryPackageContentRequest
  {
    RegistryPackageIdentity identity;
    Version version;
    PackageContentId expectedContent;
  };

  enum class PackageContentSourceStatus { available, unavailable, transport_failure };

  enum class RegistryPackageContentSourceStatus
  {
    available,
    not_found,
    transport_failure,
    permission_denied,
    configuration_error,
    payload_too_large
  };

  // `available` carries untrusted exact P10O canonical bytes. The provider
  // cannot mint a VerifiedPackageContent token.
  struct RegistryPackageContentSourceResult
  {
    RegistryPackageContentSourceStatus status{RegistryPackageContentSourceStatus::not_found};
    std::optional<std::string> canonicalBytes;
    std::string diagnostic;
  };

  class RegistryPackageContentSource
  {
  public:
    virtual ~RegistryPackageContentSource() = default;
    [[nodiscard]] virtual RegistryPackageContentSourceResult acquire(
      const RegistryContentLocation &location,
      const RegistryPackageContentRequest &request) = 0;
  };

  struct GitPackageContentRequest
  {
    std::string normalizedRepository;
    bool repositoryRoot{true};
    std::string fullCommit;
    PackageContentId expectedContent;
  };

  // The Git provider supplies an untrusted logical tree, never a verified
  // token. Acquisition is commit-only; branch/tag resolution is upstream.
  struct GitPackageContentSourceResult
  {
    PackageContentSourceStatus status{PackageContentSourceStatus::unavailable};
    std::shared_ptr<const CanonicalPackageContentCandidate> candidate;
    std::string diagnostic;
  };

  class GitPackageContentSource
  {
  public:
    virtual ~GitPackageContentSource() = default;
    [[nodiscard]] virtual GitPackageContentSourceResult acquire(
      const GitPackageContentRequest &request) = 0;
  };

  enum class PackageContentAcquisitionState { reused, acquired };

  struct PackageContentAcquisitionEntry
  {
    PackageContentId content;
    PackageContentAcquisitionState state{PackageContentAcquisitionState::reused};
    std::vector<std::string> nodeIds;
  };

  enum class PackageContentAcquisitionIssueCode
  {
    invalid_lockfile,
    unknown_registry,
    package_content_source_unavailable,
    package_content_transport_failure,
    package_content_integrity_failure,
    package_content_digest_mismatch,
    package_content_source_configuration_error,
    package_content_payload_too_large,
    package_content_git_source_failure,
    package_content_store_failure,
  };

  struct RegistryLocationAcquisitionDiagnostic
  {
    std::string locationDescriptor;
    RegistryPackageContentSourceStatus status{RegistryPackageContentSourceStatus::not_found};
    std::string message;
  };

  struct PackageContentAcquisitionIssue
  {
    PackageContentAcquisitionIssueCode code{PackageContentAcquisitionIssueCode::invalid_lockfile};
    std::string digest;
    std::vector<std::string> nodeIds;
    std::string message;
    std::vector<PackageContentIssue> contentIssues;
    std::vector<CanonicalPackageContentIssue> canonicalIssues;
    std::vector<RegistryLocationAcquisitionDiagnostic> registryLocations;
  };

  struct PackageContentAcquisitionResult
  {
    std::vector<PackageContentAcquisitionEntry> entries;
    std::vector<PackageContentAcquisitionIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return issues.empty(); }
  };

  // Ensures every unique content identity named by the exact lock is present.
  // Required content is traversed in P10N's digest-sorted plan order. For a
  // digest referenced by multiple nodes, the first sorted node ID selects the
  // source, with no cross-source fallback.
  [[nodiscard]] PackageContentAcquisitionResult acquire_locked_package_content(
    const Lockfile &lockfile,
    const RegistryDirectory &registries,
    RegistryPackageContentSource &registrySource,
    GitPackageContentSource &gitSource,
    PackageContentStore &store);
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_PACKAGE_CONTENT_ACQUISITION_HPP
