/** @file RegistrySnapshotState.hpp
 *  Pure Registry snapshot rollback checks and per-acquisition snapshot guard.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_REGISTRY_SNAPSHOT_STATE_HPP
#define VIX_CLI_PROJECT_NATIVE_REGISTRY_SNAPSHOT_STATE_HPP

#include <optional>
#include <string>
#include <vector>

#include <vix/cli/project/native/RegistryAuthority.hpp>

namespace vix::cli::project::native
{
  // Tiny security state, deliberately separate from metadata caches and locks.
  struct TrustedRegistrySnapshotState
  {
    std::string registryId;
    std::uint64_t sequence{0};
    std::string digest;
    friend bool operator==(const TrustedRegistrySnapshotState &, const TrustedRegistrySnapshotState &) = default;
  };

  enum class RegistrySnapshotStateErrorCode
  {
    invalid_trusted_snapshot_state,
    registry_rollback_detected,
    registry_snapshot_equivocation,
    registry_snapshot_changed_during_acquisition
  };

  struct RegistrySnapshotStateIssue
  {
    RegistrySnapshotStateErrorCode code{RegistrySnapshotStateErrorCode::invalid_trusted_snapshot_state};
    std::string registryId;
    std::string message;
  };

  struct RegistrySnapshotAcceptance
  {
    std::optional<VerifiedRegistryObservation> observation;
    std::optional<TrustedRegistrySnapshotState> proposedState;
    std::vector<RegistrySnapshotStateIssue> issues;
    [[nodiscard]] bool valid() const noexcept
      { return observation.has_value() && proposedState.has_value() && issues.empty(); }
  };

  // This API accepts the P10J capability type only; unverified mirror claims
  // cannot be passed to anti-rollback policy.
  [[nodiscard]] RegistrySnapshotAcceptance check_registry_snapshot_state(
    const std::optional<TrustedRegistrySnapshotState> &previous,
    const VerifiedRegistryObservation &observation);

  struct RegistrySnapshotPin
  {
    std::string registryId;
    RegistrySnapshotIdentity snapshot;
    friend bool operator==(const RegistrySnapshotPin &, const RegistrySnapshotPin &) = default;
  };

  enum class RegistrySnapshotRoutingErrorCode
  {
    unknown_registry,
    unknown_registry_authority,
    location_or_verification_failure,
    snapshot_rejected,
    all_registry_locations_rejected,
    terminal_location_or_authority_failure
  };

  struct RegistrySnapshotRoutingIssue
  {
    RegistrySnapshotRoutingErrorCode code{RegistrySnapshotRoutingErrorCode::location_or_verification_failure};
    std::string registryId;
    std::string location;
    std::string message;
  };

  // One instance is one synchronous acquisition session. Pins and proposed
  // state are ephemeral values; persistence and retry policy belong elsewhere.
  class RegistrySnapshotGuard final : public PackageMetadataSource
  {
  public:
    RegistrySnapshotGuard(const RegistryDirectory &locations,
      const RegistryAuthorityDirectory &authorities,
      RegistryObservationLocationSource &registrySource,
      const RegistryAuthorityVerifier &verifier,
      PackageMetadataSource &gitSource,
      std::vector<TrustedRegistrySnapshotState> trustedStates = {});

    [[nodiscard]] RegistryQueryResult registry_releases(const RegistryPackageIdentity &identity) override;
    [[nodiscard]] GitQueryResult resolve_git(const GitPackageIdentity &identity,
      const GitRevisionRequirement &revision) override;
    [[nodiscard]] const std::vector<RegistrySnapshotPin> &pins() const noexcept { return pins_; }
    [[nodiscard]] const std::vector<TrustedRegistrySnapshotState> &proposed_states() const noexcept
      { return proposedStates_; }
    [[nodiscard]] const std::vector<RegistrySnapshotRoutingIssue> &last_issues() const noexcept
      { return lastIssues_; }

  private:
    const RegistryDirectory &locations_;
    const RegistryAuthorityDirectory &authorities_;
    RegistryObservationLocationSource &registrySource_;
    const RegistryAuthorityVerifier &verifier_;
    PackageMetadataSource &gitSource_;
    std::vector<TrustedRegistrySnapshotState> trustedStates_;
    std::vector<RegistrySnapshotPin> pins_;
    std::vector<TrustedRegistrySnapshotState> proposedStates_;
    std::vector<RegistrySnapshotRoutingIssue> lastIssues_;
  };
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_REGISTRY_SNAPSHOT_STATE_HPP
