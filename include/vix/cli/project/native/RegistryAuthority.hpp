/** @file RegistryAuthority.hpp
 *  Registry namespace authority and verified metadata observation boundary.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_REGISTRY_AUTHORITY_HPP
#define VIX_CLI_PROJECT_NATIVE_REGISTRY_AUTHORITY_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vix/cli/project/native/MetadataAcquisition.hpp>
#include <vix/cli/project/native/RegistryDirectory.hpp>

namespace vix::cli::project::native
{
  struct RegistryAuthorityDefinition
  {
    std::string registryId;
    // Logical authority handle, never a hostname or transport endpoint.
    std::string authorityId;
  };

  enum class RegistryAuthorityDirectoryErrorCode { invalid_registry_id, empty_authority_id, duplicate_registry_authority };
  struct RegistryAuthorityDirectoryIssue
  {
    RegistryAuthorityDirectoryErrorCode code{RegistryAuthorityDirectoryErrorCode::invalid_registry_id};
    std::string registryId;
    std::string message;
  };

  class RegistryAuthorityDirectory;
  struct RegistryAuthorityDirectoryBuildResult;
  [[nodiscard]] RegistryAuthorityDirectoryBuildResult build_registry_authority_directory(
    std::vector<RegistryAuthorityDefinition> definitions);

  class RegistryAuthorityDirectory
  {
  public:
    [[nodiscard]] const RegistryAuthorityDefinition *find(std::string_view registryId) const noexcept;
  private:
    friend RegistryAuthorityDirectoryBuildResult build_registry_authority_directory(
      std::vector<RegistryAuthorityDefinition>);
    explicit RegistryAuthorityDirectory(std::vector<RegistryAuthorityDefinition> definitions);
    std::vector<RegistryAuthorityDefinition> definitions_;
  };

  struct RegistryAuthorityDirectoryBuildResult
  {
    std::optional<RegistryAuthorityDirectory> value;
    std::vector<RegistryAuthorityDirectoryIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return value.has_value() && issues.empty(); }
  };

  struct RegistrySnapshotIdentity
  {
    std::string registryId;
    std::uint64_t sequence{0};
    std::string digest;
    friend bool operator==(const RegistrySnapshotIdentity &, const RegistrySnapshotIdentity &) = default;
  };

  // The source/location boundary produces only unverified claims. Authority
  // evidence remains opaque to routing; the concrete verifier owns its encoding.
  struct UnverifiedRegistryObservation
  {
    RegistrySnapshotIdentity snapshot;
    RegistryPackageIdentity query;
    std::vector<RegistryCandidate> releases;
    std::string authorityEvidence;
  };

  enum class RegistryObservationLocationStatus
  {
    available,
    not_found,
    transport_failure,
    permission_denied,
    configuration_error,
    malformed_metadata,
    integrity_failure
  };

  struct UnverifiedRegistryLocationResult
  {
    RegistryObservationLocationStatus status{RegistryObservationLocationStatus::configuration_error};
    std::optional<UnverifiedRegistryObservation> observation;
    std::string diagnostic;
  };

  class RegistryAuthorityVerifier;
  struct RegistryObservationVerificationResult;

  class RegistryObservationLocationSource
  {
  public:
    virtual ~RegistryObservationLocationSource() = default;
    [[nodiscard]] virtual UnverifiedRegistryLocationResult registry_observation_at(
      const RegistryLocation &location, const RegistryPackageIdentity &identity) = 0;
  };

  class VerifiedRegistryObservation final
  {
  public:
    [[nodiscard]] const RegistrySnapshotIdentity &snapshot() const noexcept { return snapshot_; }
    [[nodiscard]] const RegistryPackageIdentity &query() const noexcept { return query_; }
    [[nodiscard]] const std::vector<RegistryCandidate> &releases() const noexcept { return releases_; }
  private:
    friend struct RegistryObservationVerificationResult;
    friend RegistryObservationVerificationResult verify_registry_observation(
      const RegistryAuthorityDirectory &, const RegistryAuthorityVerifier &,
      const RegistryPackageIdentity &, const UnverifiedRegistryObservation &);
    VerifiedRegistryObservation(RegistrySnapshotIdentity snapshot, RegistryPackageIdentity query,
                                std::vector<RegistryCandidate> releases)
      : snapshot_(std::move(snapshot)), query_(std::move(query)), releases_(std::move(releases)) {}
    RegistrySnapshotIdentity snapshot_;
    RegistryPackageIdentity query_;
    std::vector<RegistryCandidate> releases_;
  };

  enum class RegistryVerificationErrorCode
  {
    unknown_registry_authority,
    invalid_snapshot_identity,
    registry_binding_mismatch,
    package_query_binding_mismatch,
    authority_verification_failed
  };
  struct RegistryVerificationIssue
  {
    RegistryVerificationErrorCode code{RegistryVerificationErrorCode::authority_verification_failed};
    std::string registryId;
    std::string message;
  };

  struct RegistryObservationVerificationResult
  {
    std::optional<VerifiedRegistryObservation> observation;
    std::vector<RegistryVerificationIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return observation.has_value() && issues.empty(); }
  };

  // Implementations authenticate all semantically relevant observation data
  // against the configured logical authority; this interface chooses no crypto.
  // Successful verification proves authority and snapshot binding, not freshness.
  class RegistryAuthorityVerifier
  {
  public:
    virtual ~RegistryAuthorityVerifier() = default;
    [[nodiscard]] virtual bool verify_evidence(const RegistryAuthorityDefinition &authority,
      const UnverifiedRegistryObservation &observation, std::string &diagnostic) const = 0;
  };

  [[nodiscard]] RegistryObservationVerificationResult verify_registry_observation(
    const RegistryAuthorityDirectory &directory, const RegistryAuthorityVerifier &verifier,
    const RegistryPackageIdentity &requested, const UnverifiedRegistryObservation &observation);

  enum class VerifiedRegistryRoutingErrorCode
  {
    unknown_registry,
    unknown_registry_authority,
    location_or_verification_failure,
    all_registry_locations_untrusted_or_failed,
    terminal_location_or_authority_failure
  };
  struct VerifiedRegistryRoutingIssue
  {
    VerifiedRegistryRoutingErrorCode code{VerifiedRegistryRoutingErrorCode::location_or_verification_failure};
    std::string registryId;
    std::string location;
    std::string message;
  };
  struct VerifiedRegistryRoutingResult
  {
    std::optional<VerifiedRegistryObservation> observation;
    // Retained only for the immediate online metadata admission/cache path;
    // callers still must verify it before treating it as trusted.
    std::optional<UnverifiedRegistryObservation> unverifiedObservation;
    std::vector<VerifiedRegistryRoutingIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return observation.has_value() && issues.empty(); }
  };

  [[nodiscard]] VerifiedRegistryRoutingResult route_verified_registry_observation(
    const RegistryDirectory &locations, const RegistryAuthorityDirectory &authorities,
    RegistryObservationLocationSource &source, const RegistryAuthorityVerifier &verifier,
    const RegistryPackageIdentity &identity);

  class VerifiedRoutedPackageMetadataSource final : public PackageMetadataSource
  {
  public:
    VerifiedRoutedPackageMetadataSource(const RegistryDirectory &locations,
      const RegistryAuthorityDirectory &authorities, RegistryObservationLocationSource &registrySource,
      const RegistryAuthorityVerifier &verifier, PackageMetadataSource &gitSource);
    [[nodiscard]] RegistryQueryResult registry_releases(const RegistryPackageIdentity &identity) override;
    [[nodiscard]] GitQueryResult resolve_git(const GitPackageIdentity &identity,
      const GitRevisionRequirement &revision) override;
  private:
    const RegistryDirectory &locations_;
    const RegistryAuthorityDirectory &authorities_;
    RegistryObservationLocationSource &registrySource_;
    const RegistryAuthorityVerifier &verifier_;
    PackageMetadataSource &gitSource_;
  };
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_REGISTRY_AUTHORITY_HPP
