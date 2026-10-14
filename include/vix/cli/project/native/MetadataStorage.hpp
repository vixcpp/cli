/** @file MetadataStorage.hpp
 *  Semantic trusted-state storage and disposable package metadata cache.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_METADATA_STORAGE_HPP
#define VIX_CLI_PROJECT_NATIVE_METADATA_STORAGE_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/project/native/RegistrySnapshotState.hpp>

namespace vix::cli::project::native
{
  enum class TrustedStateStoreErrorCode { trusted_state_concurrent_update, invalid_trusted_snapshot_state };
  struct TrustedStateStoreIssue
  {
    TrustedStateStoreErrorCode code{TrustedStateStoreErrorCode::invalid_trusted_snapshot_state};
    std::string registryId;
    std::string message;
  };
  struct TrustedStateLoadResult
  {
    std::optional<TrustedRegistrySnapshotState> state;
    std::vector<TrustedStateStoreIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return issues.empty(); }
  };
  struct TrustedStateCommitResult
  {
    bool committed{false};
    std::vector<TrustedStateStoreIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return committed && issues.empty(); }
  };

  // Security state is keyed only by logical Registry ID. Implementations must
  // compare-and-advance atomically; a blind overwrite API is intentionally absent.
  class TrustedRegistryStateStore
  {
  public:
    virtual ~TrustedRegistryStateStore() = default;
    [[nodiscard]] virtual TrustedStateLoadResult load(std::string_view registryId) const = 0;
    [[nodiscard]] virtual TrustedStateCommitResult compare_and_advance(
      const std::optional<TrustedRegistrySnapshotState> &expected,
      const TrustedRegistrySnapshotState &next) = 0;
  };

  class InMemoryTrustedRegistryStateStore final : public TrustedRegistryStateStore
  {
  public:
    [[nodiscard]] TrustedStateLoadResult load(std::string_view registryId) const override;
    [[nodiscard]] TrustedStateCommitResult compare_and_advance(
      const std::optional<TrustedRegistrySnapshotState> &expected,
      const TrustedRegistrySnapshotState &next) override;
  private:
    std::vector<TrustedRegistrySnapshotState> states_;
  };

  enum class MetadataCacheErrorCode
  {
    cache_conflict,
    invalid_registry_cache_key,
    invalid_git_cache_key,
    offline_trusted_state_unavailable,
    offline_registry_metadata_unavailable,
    offline_git_metadata_unavailable,
    offline_cached_metadata_trust_failure,
    online_registry_observation_rejected,
    registry_snapshot_changed_during_acquisition
  };
  struct MetadataCacheIssue
  {
    MetadataCacheErrorCode code{MetadataCacheErrorCode::cache_conflict};
    std::string subject;
    std::string message;
  };
  template <typename T>
  struct MetadataStorageResult
  {
    std::optional<T> value;
    std::vector<MetadataCacheIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return value.has_value() && issues.empty(); }
  };

  struct RegistryMetadataCacheEntry
  {
    UnverifiedRegistryObservation observation;
  };
  struct GitMetadataCacheEntry
  {
    GitPackageIdentity identity;
    GitRevisionRequirement revision;
    GitResolution resolution;
  };

  class PackageMetadataCache;
  struct OnlineRegistryAdmission;

  class RegistryObservationCacheAdmission final
  {
  public:
    [[nodiscard]] const UnverifiedRegistryObservation &observation() const noexcept { return observation_; }
    [[nodiscard]] const TrustedRegistrySnapshotState &proposed_state() const noexcept { return proposedState_; }
  private:
    friend struct OnlineRegistryAdmission;
    friend OnlineRegistryAdmission admit_online_registry_observation(
      const RegistryAuthorityDirectory &, const RegistryAuthorityVerifier &,
      const std::optional<TrustedRegistrySnapshotState> &, const RegistryPackageIdentity &,
      const UnverifiedRegistryObservation &, PackageMetadataCache &);
    RegistryObservationCacheAdmission(UnverifiedRegistryObservation observation,
      TrustedRegistrySnapshotState proposedState)
      : observation_(std::move(observation)), proposedState_(std::move(proposedState)) {}
    UnverifiedRegistryObservation observation_;
    TrustedRegistrySnapshotState proposedState_;
  };

  // Cache entries are observations, not trust. Registry evidence is retained
  // so every read can cross P10J again. Cache lifetime is independent of the
  // trusted-state store and the later package-content store.
  class PackageMetadataCache
  {
  public:
    virtual ~PackageMetadataCache() = default;
    [[nodiscard]] virtual MetadataStorageResult<UnverifiedRegistryObservation> read_registry(
      const RegistrySnapshotIdentity &snapshot, const RegistryPackageIdentity &query) const = 0;
    [[nodiscard]] virtual MetadataStorageResult<bool> write_registry(
      const RegistryObservationCacheAdmission &admission) = 0;
    [[nodiscard]] virtual MetadataStorageResult<GitResolution> read_git(
      const GitPackageIdentity &identity, const GitRevisionRequirement &revision) const = 0;
    [[nodiscard]] virtual MetadataStorageResult<bool> write_git(
      const GitPackageIdentity &identity, const GitRevisionRequirement &revision,
      const GitResolution &resolution) = 0;
  };

  class InMemoryPackageMetadataCache final : public PackageMetadataCache
  {
  public:
    [[nodiscard]] MetadataStorageResult<UnverifiedRegistryObservation> read_registry(
      const RegistrySnapshotIdentity &snapshot, const RegistryPackageIdentity &query) const override;
    [[nodiscard]] MetadataStorageResult<bool> write_registry(
      const RegistryObservationCacheAdmission &admission) override;
    [[nodiscard]] MetadataStorageResult<GitResolution> read_git(
      const GitPackageIdentity &identity, const GitRevisionRequirement &revision) const override;
    [[nodiscard]] MetadataStorageResult<bool> write_git(const GitPackageIdentity &identity,
      const GitRevisionRequirement &revision, const GitResolution &resolution) override;
    void clear() noexcept;
  private:
    std::vector<RegistryMetadataCacheEntry> registryEntries_;
    std::vector<GitMetadataCacheEntry> gitEntries_;
  };

  enum class MetadataAccessMode { online, offline };

  // Pure online admission gate: unverified source data is cached only after
  // P10J verification and P10K acceptance. The caller separately commits the
  // returned proposed state through compare_and_advance.
  struct OnlineRegistryAdmission
  {
    std::optional<TrustedRegistrySnapshotState> proposedState;
    std::optional<RegistryObservationCacheAdmission> cacheAdmission;
    std::vector<MetadataCacheIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return proposedState.has_value() && issues.empty(); }
  };
  [[nodiscard]] OnlineRegistryAdmission admit_online_registry_observation(
    const RegistryAuthorityDirectory &authorities, const RegistryAuthorityVerifier &verifier,
    const std::optional<TrustedRegistrySnapshotState> &previous,
    const RegistryPackageIdentity &query, const UnverifiedRegistryObservation &observation,
    PackageMetadataCache &cache);

  // Offline adapter never calls an external source. Registry cache lookups use
  // exactly the persisted trusted snapshot and are re-verified and pinned.
  class OfflineCachedPackageMetadataSource final : public PackageMetadataSource
  {
  public:
    OfflineCachedPackageMetadataSource(const TrustedRegistryStateStore &trustedStates,
      const PackageMetadataCache &cache, const RegistryAuthorityDirectory &authorities,
      const RegistryAuthorityVerifier &verifier);
    [[nodiscard]] RegistryQueryResult registry_releases(const RegistryPackageIdentity &identity) override;
    [[nodiscard]] GitQueryResult resolve_git(const GitPackageIdentity &identity,
      const GitRevisionRequirement &revision) override;
    [[nodiscard]] const std::vector<RegistrySnapshotPin> &pins() const noexcept { return pins_; }
    [[nodiscard]] const std::vector<MetadataCacheIssue> &last_issues() const noexcept { return lastIssues_; }
  private:
    const TrustedRegistryStateStore &trustedStates_;
    const PackageMetadataCache &cache_;
    const RegistryAuthorityDirectory &authorities_;
    const RegistryAuthorityVerifier &verifier_;
    std::vector<RegistrySnapshotPin> pins_;
    std::vector<MetadataCacheIssue> lastIssues_;
  };
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_METADATA_STORAGE_HPP
