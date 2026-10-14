#include <vix/cli/project/native/MetadataStorage.hpp>

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <utility>

#include <vix/cli/project/native/Validation.hpp>

namespace vix::cli::project::native
{
  namespace
  {
    MetadataCacheIssue issue(MetadataCacheErrorCode code, std::string subject, std::string message)
    { return {code, std::move(subject), std::move(message)}; }

    bool same_revision(const GitRevisionRequirement &left, const GitRevisionRequirement &right)
    {
      if (left.index() != right.index()) return false;
      return std::visit([&](const auto &value)
      {
        using Revision = std::decay_t<decltype(value)>;
        const auto *other = std::get_if<Revision>(&right);
        return other && value.value == other->value;
      }, left);
    }

    auto registry_entry(const std::vector<RegistryMetadataCacheEntry> &entries,
      const RegistrySnapshotIdentity &snapshot, const RegistryPackageIdentity &query)
    {
      return std::find_if(entries.begin(), entries.end(), [&](const auto &entry)
      { return entry.observation.snapshot == snapshot && entry.observation.query == query; });
    }

    auto git_entry(const std::vector<GitMetadataCacheEntry> &entries,
      const GitPackageIdentity &identity, const GitRevisionRequirement &revision)
    {
      return std::find_if(entries.begin(), entries.end(), [&](const auto &entry)
      { return entry.identity == identity && same_revision(entry.revision, revision); });
    }

    std::string revision_text(const GitRevisionRequirement &revision)
    {
      return std::visit([](const auto &value) { return value.value; }, revision);
    }

    bool valid_digest(std::string_view value)
    {
      const auto colon = value.find(':');
      if (colon == std::string_view::npos) return false;
      const auto algorithm = value.substr(0, colon);
      const auto hex = value.substr(colon + 1);
      const bool rightLength = (algorithm == "sha256" && hex.size() == 64) ||
        (algorithm == "sha512" && hex.size() == 128);
      return rightLength && std::all_of(hex.begin(), hex.end(), [](char c)
        { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }
  }

  TrustedStateLoadResult InMemoryTrustedRegistryStateStore::load(std::string_view registryId) const
  {
    const auto found = std::find_if(states_.begin(), states_.end(), [&](const auto &state)
      { return state.registryId == registryId; });
    return {found == states_.end() ? std::nullopt : std::optional<TrustedRegistrySnapshotState>{*found}, {}};
  }

  TrustedStateCommitResult InMemoryTrustedRegistryStateStore::compare_and_advance(
    const std::optional<TrustedRegistrySnapshotState> &expected,
    const TrustedRegistrySnapshotState &next)
  {
    if (!valid_registry_id(next.registryId) || next.sequence == 0 || !valid_digest(next.digest) ||
        (expected && (expected->registryId != next.registryId || expected->sequence == 0 || !valid_digest(expected->digest))))
      return {false, {{TrustedStateStoreErrorCode::invalid_trusted_snapshot_state,
        next.registryId, "next trusted snapshot state is structurally invalid"}}};
    const auto found = std::find_if(states_.begin(), states_.end(), [&](const auto &state)
      { return state.registryId == next.registryId; });
    const std::optional<TrustedRegistrySnapshotState> current = found == states_.end()
      ? std::nullopt : std::optional<TrustedRegistrySnapshotState>{*found};
    if (current != expected)
      return {false, {{TrustedStateStoreErrorCode::trusted_state_concurrent_update,
        next.registryId, "persisted trusted state differs from the caller's expected baseline"}}};
    if (found == states_.end()) states_.push_back(next);
    else *found = next;
    std::sort(states_.begin(), states_.end(), [](const auto &a, const auto &b)
      { return a.registryId < b.registryId; });
    return {true, {}};
  }

  MetadataStorageResult<UnverifiedRegistryObservation> InMemoryPackageMetadataCache::read_registry(
    const RegistrySnapshotIdentity &snapshot, const RegistryPackageIdentity &query) const
  {
    const auto found = registry_entry(registryEntries_, snapshot, query);
    if (found == registryEntries_.end()) return {};
    return {found->observation, {}};
  }

  MetadataStorageResult<bool> InMemoryPackageMetadataCache::write_registry(
    const RegistryObservationCacheAdmission &admission)
  {
    const auto &observation = admission.observation();
    if (observation.snapshot.registryId != observation.query.registry ||
        observation.snapshot.registryId != admission.proposed_state().registryId ||
        observation.snapshot.sequence != admission.proposed_state().sequence ||
        observation.snapshot.digest != admission.proposed_state().digest)
      return {std::nullopt, {issue(MetadataCacheErrorCode::invalid_registry_cache_key,
        observation.snapshot.registryId, "admitted observation and proposed trusted state do not share a snapshot identity")}};
    if (registry_entry(registryEntries_, observation.snapshot, observation.query) != registryEntries_.end())
      return {std::nullopt, {issue(MetadataCacheErrorCode::cache_conflict,
        observation.query.registry + "/" + observation.query.publisher + "/" + observation.query.name,
        "Registry cache key already exists; insertion never overwrites an observation")}};
    registryEntries_.push_back({observation});
    std::sort(registryEntries_.begin(), registryEntries_.end(), [](const auto &a, const auto &b)
    {
      const auto &x = a.observation;
      const auto &y = b.observation;
      if (x.snapshot.registryId != y.snapshot.registryId) return x.snapshot.registryId < y.snapshot.registryId;
      if (x.snapshot.sequence != y.snapshot.sequence) return x.snapshot.sequence < y.snapshot.sequence;
      if (x.snapshot.digest != y.snapshot.digest) return x.snapshot.digest < y.snapshot.digest;
      if (x.query.publisher != y.query.publisher) return x.query.publisher < y.query.publisher;
      return x.query.name < y.query.name;
    });
    return {true, {}};
  }

  MetadataStorageResult<GitResolution> InMemoryPackageMetadataCache::read_git(
    const GitPackageIdentity &identity, const GitRevisionRequirement &revision) const
  {
    const auto found = git_entry(gitEntries_, identity, revision);
    if (found == gitEntries_.end()) return {};
    return {found->resolution, {}};
  }

  MetadataStorageResult<bool> InMemoryPackageMetadataCache::write_git(
    const GitPackageIdentity &identity, const GitRevisionRequirement &revision,
    const GitResolution &resolution)
  {
    if (!identity.repositoryRoot || resolution.normalizedRepository != identity.normalizedRepository ||
        !same_revision(resolution.requestedRevision, revision))
      return {std::nullopt, {issue(MetadataCacheErrorCode::invalid_git_cache_key,
        identity.normalizedRepository, "Git cache key does not match the successful immutable resolution")}};
    if (git_entry(gitEntries_, identity, revision) != gitEntries_.end())
      return {std::nullopt, {issue(MetadataCacheErrorCode::cache_conflict,
        identity.normalizedRepository + "#" + revision_text(revision),
        "Git cache key already exists; insertion never overwrites a resolution")}};
    gitEntries_.push_back({identity, revision, resolution});
    std::sort(gitEntries_.begin(), gitEntries_.end(), [](const auto &a, const auto &b)
    {
      if (a.identity.normalizedRepository != b.identity.normalizedRepository)
        return a.identity.normalizedRepository < b.identity.normalizedRepository;
      if (a.revision.index() != b.revision.index()) return a.revision.index() < b.revision.index();
      return revision_text(a.revision) < revision_text(b.revision);
    });
    return {true, {}};
  }

  void InMemoryPackageMetadataCache::clear() noexcept
  { registryEntries_.clear(); gitEntries_.clear(); }

  OnlineRegistryAdmission admit_online_registry_observation(
    const RegistryAuthorityDirectory &authorities, const RegistryAuthorityVerifier &verifier,
    const std::optional<TrustedRegistrySnapshotState> &previous,
    const RegistryPackageIdentity &query, const UnverifiedRegistryObservation &observation,
    PackageMetadataCache &cache)
  {
    OnlineRegistryAdmission result;
    const auto verified = verify_registry_observation(authorities, verifier, query, observation);
    if (!verified.valid())
    {
      result.issues.push_back(issue(MetadataCacheErrorCode::online_registry_observation_rejected,
        query.registry + "/" + query.publisher + "/" + query.name,
        verified.issues.empty() ? "Registry observation failed authority verification" : verified.issues.front().message));
      return result;
    }
    const auto accepted = check_registry_snapshot_state(previous, *verified.observation);
    if (!accepted.valid())
    {
      result.issues.push_back(issue(MetadataCacheErrorCode::online_registry_observation_rejected,
        query.registry + "/" + query.publisher + "/" + query.name,
        accepted.issues.empty() ? "Registry snapshot failed anti-rollback acceptance" : accepted.issues.front().message));
      return result;
    }
    RegistryObservationCacheAdmission token{observation, *accepted.proposedState};
    const auto cached = cache.write_registry(token);
    if (!cached.valid())
    {
      result.issues = cached.issues;
      return result;
    }
    result.proposedState = *accepted.proposedState;
    result.cacheAdmission = std::move(token);
    return result;
  }

  OfflineCachedPackageMetadataSource::OfflineCachedPackageMetadataSource(
    const TrustedRegistryStateStore &trustedStates, const PackageMetadataCache &cache,
    const RegistryAuthorityDirectory &authorities, const RegistryAuthorityVerifier &verifier)
    : trustedStates_(trustedStates), cache_(cache), authorities_(authorities), verifier_(verifier) {}

  RegistryQueryResult OfflineCachedPackageMetadataSource::registry_releases(const RegistryPackageIdentity &identity)
  {
    lastIssues_.clear();
    const auto trusted = trustedStates_.load(identity.registry);
    if (!trusted.valid())
    {
      lastIssues_.push_back(issue(MetadataCacheErrorCode::offline_trusted_state_unavailable,
        identity.registry, "trusted Registry baseline could not be loaded"));
      return {false, {}, "offline_trusted_state_unavailable [" + identity.registry + "]"};
    }
    if (!trusted.state)
    {
      lastIssues_.push_back(issue(MetadataCacheErrorCode::offline_trusted_state_unavailable,
        identity.registry, "no trusted snapshot baseline exists; cache cannot bootstrap Registry trust"));
      return {false, {}, "offline_trusted_state_unavailable [" + identity.registry + "]"};
    }
    const RegistrySnapshotIdentity snapshot{trusted.state->registryId, trusted.state->sequence, trusted.state->digest};
    auto cached = cache_.read_registry(snapshot, identity);
    if (!cached.issues.empty())
    {
      lastIssues_ = cached.issues;
      return {false, {}, "offline_cached_metadata_trust_failure [" + identity.registry + "]: cache read failed"};
    }
    if (!cached.value)
    {
      lastIssues_.push_back(issue(MetadataCacheErrorCode::offline_registry_metadata_unavailable,
        identity.registry + "/" + identity.publisher + "/" + identity.name,
        "no cached observation exists for the exact trusted Registry snapshot"));
      return {false, {}, "offline_registry_metadata_unavailable [" + identity.registry + "]"};
    }
    const auto verified = verify_registry_observation(authorities_, verifier_, identity, *cached.value);
    if (!verified.valid())
    {
      lastIssues_.push_back(issue(MetadataCacheErrorCode::offline_cached_metadata_trust_failure,
        identity.registry + "/" + identity.publisher + "/" + identity.name,
        verified.issues.empty() ? "cached Registry observation failed authority verification" : verified.issues.front().message));
      return {false, {}, "offline_cached_metadata_trust_failure [" + identity.registry + "]"};
    }
    const auto accepted = check_registry_snapshot_state(*trusted.state, *verified.observation);
    if (!accepted.valid() || accepted.observation->snapshot() != snapshot)
    {
      const auto message = !accepted.issues.empty() ? accepted.issues.front().message :
        "cached observation does not match the exact persisted trusted snapshot";
      lastIssues_.push_back(issue(MetadataCacheErrorCode::offline_cached_metadata_trust_failure,
        identity.registry, message));
      return {false, {}, "offline_cached_metadata_trust_failure [" + identity.registry + "]"};
    }
    const auto pin = std::find_if(pins_.begin(), pins_.end(), [&](const auto &entry)
      { return entry.registryId == identity.registry; });
    if (pin != pins_.end() && pin->snapshot != accepted.observation->snapshot())
    {
      lastIssues_.push_back(issue(MetadataCacheErrorCode::registry_snapshot_changed_during_acquisition,
        identity.registry, "cached Registry query differs from the acquisition session snapshot pin"));
      return {false, {}, "registry_snapshot_changed_during_acquisition [" + identity.registry + "]"};
    }
    if (pin == pins_.end()) pins_.push_back({identity.registry, accepted.observation->snapshot()});
    std::sort(pins_.begin(), pins_.end(), [](const auto &a, const auto &b) { return a.registryId < b.registryId; });
    return {true, accepted.observation->releases(), {}};
  }

  GitQueryResult OfflineCachedPackageMetadataSource::resolve_git(
    const GitPackageIdentity &identity, const GitRevisionRequirement &revision)
  {
    lastIssues_.clear();
    const auto cached = cache_.read_git(identity, revision);
    if (!cached.issues.empty())
    {
      lastIssues_ = cached.issues;
      return {GitQueryStatus::failed, std::nullopt,
        "offline_git_metadata_unavailable [" + identity.normalizedRepository + "]: cache read failed"};
    }
    if (!cached.value)
    {
      lastIssues_.push_back(issue(MetadataCacheErrorCode::offline_git_metadata_unavailable,
        identity.normalizedRepository + "#" + revision_text(revision),
        "no successful cached immutable resolution exists for this Git declaration"));
      return {GitQueryStatus::failed, std::nullopt,
        "offline_git_metadata_unavailable [" + identity.normalizedRepository + "]"};
    }
    // Branch/tag cache values are last observed mappings, not freshness claims.
    return {GitQueryStatus::found, cached.value, {}};
  }
} // namespace vix::cli::project::native
