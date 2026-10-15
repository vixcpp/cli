#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/Lockfile.hpp>
#include <vix/cli/project/native/MetadataAcquisition.hpp>
#include <vix/cli/project/native/MetadataStorage.hpp>
#include <vix/cli/project/native/RegistryAuthority.hpp>
#include <vix/cli/project/native/Resolver.hpp>
#include <vix/cli/project/native/TomlParser.hpp>

#include <cassert>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace vix::cli::project::native;

namespace
{
  constexpr const char *digestA = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  constexpr const char *digestB = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  const RegistryPackageIdentity xIdentity{"vix", "example", "x"};

  struct BindingVerifier final : RegistryAuthorityVerifier
  {
    bool verify_evidence(const RegistryAuthorityDefinition &, const UnverifiedRegistryObservation &value,
      std::string &diagnostic) const override
    {
      auto expected = value.snapshot.registryId + "|" + std::to_string(value.snapshot.sequence) + "|" +
        value.snapshot.digest + "|" + value.query.registry + "|" + value.query.publisher + "|" + value.query.name;
      for (const auto &release : value.releases)
        expected += "|" + release.identity.name + "|" + std::to_string(release.version.major) + "." +
          std::to_string(release.version.minor) + "." + std::to_string(release.version.patch) + "|" + release.contentDigest;
      if (value.authorityEvidence == expected) return true;
      diagnostic = "cached observation evidence does not bind its semantic contents";
      return false;
    }
  };

  std::string evidence_for(const UnverifiedRegistryObservation &value)
  {
    auto result = value.snapshot.registryId + "|" + std::to_string(value.snapshot.sequence) + "|" +
      value.snapshot.digest + "|" + value.query.registry + "|" + value.query.publisher + "|" + value.query.name;
    for (const auto &release : value.releases)
      result += "|" + release.identity.name + "|" + std::to_string(release.version.major) + "." +
        std::to_string(release.version.minor) + "." + std::to_string(release.version.patch) + "|" + release.contentDigest;
    return result;
  }

  PackageContract contract(const RegistryPackageIdentity &identity, Version version,
    std::vector<PackageRequirement> packages = {})
  {
    PackageContract value;
    value.identity = RegistryPackageContractIdentity{identity, version};
    value.packages = std::move(packages);
    value.integration = NativeVixIntegration{};
    value.exports.push_back({"api", NativeExportMapping{"api"}});
    return value;
  }

  RegistryCandidate release(const RegistryPackageIdentity &identity, std::string contentDigest = digestA,
    std::vector<PackageRequirement> packages = {})
  {
    const Version version{1, 0, 0, std::nullopt};
    return {identity, version, std::move(contentDigest), contract(identity, version, std::move(packages))};
  }

  PackageContract git_contract()
  {
    PackageContract value;
    value.identity = GitPackageContractIdentity{};
    value.integration = NativeVixIntegration{};
    value.exports.push_back({"api", NativeExportMapping{"api"}});
    return value;
  }

  UnverifiedRegistryObservation observation(std::uint64_t sequence, std::string snapshotDigest,
    RegistryPackageIdentity query = xIdentity, std::vector<RegistryCandidate> releases = {})
  {
    UnverifiedRegistryObservation value{{query.registry, sequence, std::move(snapshotDigest)}, query,
      std::move(releases), {}};
    value.authorityEvidence = evidence_for(value);
    return value;
  }

  PackageRequirement registry_requirement(std::string alias, RegistryPackageIdentity identity)
  { return {std::move(alias), RegistryRequirement{std::move(identity), ExactVersion{{1, 0, 0, std::nullopt}}}}; }

  PackageRequirement git_requirement(std::string alias, const GitPackageIdentity &identity,
    GitRevisionRequirement revision)
  { return {std::move(alias), GitRequirement{identity.normalizedRepository, identity.repositoryRoot, std::move(revision)}}; }

  struct NeverExternalSource final : PackageMetadataSource
  {
    unsigned registryCalls{0};
    unsigned gitCalls{0};
    RegistryQueryResult registry_releases(const RegistryPackageIdentity &) override
      { ++registryCalls; return {false, {}, "offline adapter called external Registry source"}; }
    GitQueryResult resolve_git(const GitPackageIdentity &, const GitRevisionRequirement &) override
      { ++gitCalls; return {GitQueryStatus::failed, std::nullopt, "offline adapter called external Git source"}; }
  };

  struct FixedLocationSource final : RegistryObservationLocationSource
  {
    UnverifiedRegistryObservation value;
    std::vector<std::string> locations;
    bool fail{false};
    UnverifiedRegistryLocationResult registry_observation_at(
      const RegistryLocation &location, const RegistryPackageIdentity &) override
    {
      locations.push_back(location.descriptor);
      if (fail) return {RegistryObservationLocationStatus::transport_failure, std::nullopt, "deliberate location source failure"};
      return {RegistryObservationLocationStatus::available, value, {}};
    }
  };

  struct CountingCache final : PackageMetadataCache
  {
    InMemoryPackageMetadataCache backing;
    mutable unsigned registryReads{0};
    mutable unsigned gitReads{0};
    MetadataStorageResult<UnverifiedRegistryObservation> read_registry(
      const RegistrySnapshotIdentity &snapshot, const RegistryPackageIdentity &query) const override
    { ++registryReads; return backing.read_registry(snapshot, query); }
    MetadataStorageResult<bool> write_registry(const RegistryObservationCacheAdmission &admission) override
    { return backing.write_registry(admission); }
    MetadataStorageResult<GitResolution> read_git(const GitPackageIdentity &identity,
      const GitRevisionRequirement &revision) const override
    { ++gitReads; return backing.read_git(identity, revision); }
    MetadataStorageResult<bool> write_git(const GitPackageIdentity &identity,
      const GitRevisionRequirement &revision, const GitResolution &resolution) override
    { return backing.write_git(identity, revision, resolution); }
  };

  struct LockPipelineResult
  {
    Lockfile lock;
    std::string text;
  };

  LockPipelineResult resolve_offline_lock(const TrustedRegistryStateStore &trusted,
    const PackageMetadataCache &cache, const RegistryAuthorityDirectory &authorities,
    const RegistryAuthorityVerifier &verifier, const std::vector<PackageRequirement> &roots)
  {
    OfflineCachedPackageMetadataSource offline(trusted, cache, authorities, verifier);
    NeverExternalSource online;
    auto acquired = acquire_resolution_catalog(roots, AcquisitionMode::offline, online, offline);
    assert(online.registryCalls == 0 && online.gitCalls == 0);
    assert(acquired.valid() && acquired.catalog);
    auto graph = resolve_packages(roots, *acquired.catalog);
    assert(graph.valid());
    auto lock = make_resolved_lockfile(*graph.value);
    assert(lock.valid() && validate_lockfile_integrity(*lock.value).valid());
    auto serialized = serialize_lockfile(*lock.value);
    assert(serialized.valid() && serialized.text);
    return {*lock.value, *serialized.text};
  }

  static_assert(!std::is_constructible_v<OfflineCachedPackageMetadataSource,
    const TrustedRegistryStateStore &, const PackageMetadataCache &,
    const RegistryAuthorityDirectory &, const RegistryAuthorityVerifier &, NeverExternalSource &>);

  struct TamperingCache final : PackageMetadataCache
  {
    InMemoryPackageMetadataCache backing;
    bool tamper{false};
    MetadataStorageResult<UnverifiedRegistryObservation> read_registry(
      const RegistrySnapshotIdentity &snapshot, const RegistryPackageIdentity &query) const override
    {
      auto result = backing.read_registry(snapshot, query);
      if (tamper && result.value && !result.value->releases.empty())
        result.value->releases.front().contentDigest = digestB;
      return result;
    }
    MetadataStorageResult<bool> write_registry(const RegistryObservationCacheAdmission &admission) override
      { return backing.write_registry(admission); }
    MetadataStorageResult<GitResolution> read_git(const GitPackageIdentity &identity,
      const GitRevisionRequirement &revision) const override { return backing.read_git(identity, revision); }
    MetadataStorageResult<bool> write_git(const GitPackageIdentity &identity,
      const GitRevisionRequirement &revision, const GitResolution &resolution) override
      { return backing.write_git(identity, revision, resolution); }
  };

  void trusted_store_tests()
  {
    InMemoryTrustedRegistryStateStore store;
    assert(!store.load("vix").state);
    const TrustedRegistrySnapshotState ten{"vix", 10, digestA};
    assert(store.compare_and_advance(std::nullopt, ten).valid());
    assert(store.load("vix").state == ten);
    const TrustedRegistrySnapshotState eleven{"vix", 11, digestB};
    assert(store.compare_and_advance(ten, eleven).valid());
    const TrustedRegistrySnapshotState twelve{"vix", 12, digestA};
    assert(store.compare_and_advance(eleven, twelve).valid());
    auto stale = store.compare_and_advance(ten, TrustedRegistrySnapshotState{"vix", 11, digestB});
    assert(!stale.valid() && stale.issues.front().code == TrustedStateStoreErrorCode::trusted_state_concurrent_update);
    assert(store.load("vix").state == twelve);
    const TrustedRegistrySnapshotState acme{"acme", 4, digestB};
    assert(store.compare_and_advance(std::nullopt, acme).valid());
    assert(store.load("vix").state == twelve && store.load("acme").state == acme);
  }

  void cache_and_offline_tests()
  {
    InMemoryTrustedRegistryStateStore trusted;
    InMemoryPackageMetadataCache cache;
    auto authorityResult = build_registry_authority_directory({{"vix", "vix-authority"}});
    assert(authorityResult.valid());
    BindingVerifier verifier;
    const TrustedRegistrySnapshotState fifty{"vix", 50, digestA};
    const auto package = release(xIdentity);
    auto admitted50 = admit_online_registry_observation(*authorityResult.value, verifier, std::nullopt,
      xIdentity, observation(50, digestA, xIdentity, {package}), cache);
    assert(admitted50.valid() && admitted50.proposedState == fifty);
    assert(trusted.compare_and_advance(std::nullopt, fifty).valid());

    const TrustedRegistrySnapshotState fiftyOne{"vix", 51, digestB};
    auto admitted51 = admit_online_registry_observation(*authorityResult.value, verifier, fifty,
      xIdentity, observation(51, digestB, xIdentity, {release(xIdentity, digestB)}), cache);
    assert(admitted51.valid() && admitted51.proposedState == fiftyOne);
    auto duplicate = admit_online_registry_observation(*authorityResult.value, verifier, std::nullopt,
      xIdentity, observation(50, digestA, xIdentity, {package}), cache);
    assert(!duplicate.valid() && duplicate.issues.front().code == MetadataCacheErrorCode::cache_conflict);
    // Newer cache entries do not replace the persisted baseline.
    NeverExternalSource external;
    OfflineCachedPackageMetadataSource offline(trusted, cache, *authorityResult.value, verifier);
    NeverExternalSource online;
    auto read50 = offline.registry_releases(xIdentity);
    assert(read50.succeeded && read50.releases.size() == 1 && read50.releases.front().contentDigest == digestA);
    assert(trusted.load("vix").state == fifty);
    assert(external.registryCalls == 0 && external.gitCalls == 0);
    auto catalogHit = acquire_resolution_catalog({registry_requirement("x", xIdentity)},
      AcquisitionMode::offline, online, offline);
    assert(catalogHit.valid() && catalogHit.catalog);
    assert(online.registryCalls == 0 && online.gitCalls == 0);

    // A second query at the exact trusted snapshot can be an authenticated empty result.
    const RegistryPackageIdentity emptyIdentity{"vix", "example", "empty"};
    auto admittedEmpty = admit_online_registry_observation(*authorityResult.value, verifier, fifty,
      emptyIdentity, observation(50, digestA, emptyIdentity, {}), cache);
    assert(admittedEmpty.valid());
    auto empty = offline.registry_releases(emptyIdentity);
    assert(empty.succeeded && empty.releases.empty());
    auto emptyCatalog = acquire_resolution_catalog({registry_requirement("empty", emptyIdentity)},
      AcquisitionMode::offline, online, offline);
    assert(emptyCatalog.valid() && emptyCatalog.catalog);
    auto noCandidate = resolve_packages({registry_requirement("empty", emptyIdentity)}, *emptyCatalog.catalog);
    assert(!noCandidate.valid() && noCandidate.issues.front().code == ResolverErrorCode::no_candidate);

    cache.clear();
    assert(trusted.load("vix").state == fifty); // cache eviction is not a trust reset
    auto miss = offline.registry_releases(xIdentity);
    assert(!miss.succeeded && offline.last_issues().front().code == MetadataCacheErrorCode::offline_registry_metadata_unavailable);
    assert(miss.diagnostic.find("no_candidate") == std::string::npos);
    auto catalogMiss = acquire_resolution_catalog({registry_requirement("x", xIdentity)},
      AcquisitionMode::offline, online, offline);
    assert(!catalogMiss.valid() && !catalogMiss.catalog);
    assert(online.registryCalls == 0 && online.gitCalls == 0);

    // Repopulate only 50/A, then advance trust: offline lookup must not select it.
    auto repopulated = admit_online_registry_observation(*authorityResult.value, verifier, fifty,
      xIdentity, observation(50, digestA, xIdentity, {package}), cache);
    assert(repopulated.valid());
    assert(trusted.compare_and_advance(fifty, fiftyOne).valid());
    auto oldOnly = offline.registry_releases(xIdentity);
    assert(!oldOnly.succeeded && offline.last_issues().front().code == MetadataCacheErrorCode::offline_registry_metadata_unavailable);
  }

  void no_trust_and_tamper_tests()
  {
    InMemoryTrustedRegistryStateStore emptyStore;
    auto authorities = build_registry_authority_directory({{"vix", "vix-authority"}});
    assert(authorities.valid());
    BindingVerifier verifier;
    InMemoryPackageMetadataCache cache;
    auto seeded = admit_online_registry_observation(*authorities.value, verifier, std::nullopt,
      xIdentity, observation(50, digestA, xIdentity, {release(xIdentity)}), cache);
    assert(seeded.valid());
    OfflineCachedPackageMetadataSource noTrust(emptyStore, cache, *authorities.value, verifier);
    auto unavailable = noTrust.registry_releases(xIdentity);
    assert(!unavailable.succeeded && noTrust.last_issues().front().code == MetadataCacheErrorCode::offline_trusted_state_unavailable);

    InMemoryTrustedRegistryStateStore trusted;
    assert(trusted.compare_and_advance(std::nullopt, *seeded.proposedState).valid());
    TamperingCache tampered;
    auto again = admit_online_registry_observation(*authorities.value, verifier, std::nullopt,
      xIdentity, observation(50, digestA, xIdentity, {release(xIdentity)}), tampered);
    assert(again.valid());
    tampered.tamper = true;
    OfflineCachedPackageMetadataSource offline(trusted, tampered, *authorities.value, verifier);
    auto result = offline.registry_releases(xIdentity);
    assert(!result.succeeded && offline.last_issues().front().code == MetadataCacheErrorCode::offline_cached_metadata_trust_failure);
  }

  void git_offline_tests()
  {
    InMemoryTrustedRegistryStateStore trusted;
    InMemoryPackageMetadataCache cache;
    auto authorities = build_registry_authority_directory({{"vix", "vix-authority"}});
    assert(authorities.valid());
    BindingVerifier verifier;
    GitPackageIdentity identity{"https://example.invalid/org/repo.git", true};
    GitRevisionRequirement branch = BranchRevision{"main"};
    GitResolution resolution{identity.normalizedRepository, branch,
      "0123456789abcdef0123456789abcdef01234567", digestA, git_contract()};
    assert(cache.write_git(identity, branch, resolution).valid());
    OfflineCachedPackageMetadataSource offline(trusted, cache, *authorities.value, verifier);
    auto hit = offline.resolve_git(identity, branch);
    assert(hit.status == GitQueryStatus::found && hit.resolution->commit == resolution.commit);
    NeverExternalSource online;
    auto catalogHit = acquire_resolution_catalog({git_requirement("git", identity, branch)},
      AcquisitionMode::offline, online, offline);
    assert(catalogHit.valid() && catalogHit.catalog);
    auto miss = offline.resolve_git(identity, TagRevision{"missing"});
    assert(miss.status == GitQueryStatus::failed &&
      miss.diagnostic.find("offline_git_metadata_unavailable") != std::string::npos);
    assert(miss.diagnostic.find("git_revision_unresolved") == std::string::npos);
    auto catalogMiss = acquire_resolution_catalog({git_requirement("git-missing", identity, TagRevision{"missing"})},
      AcquisitionMode::offline, online, offline);
    assert(!catalogMiss.valid() && !catalogMiss.catalog);
    assert(online.registryCalls == 0 && online.gitCalls == 0);
    // Branch/tag mappings are cached observations; no freshness is asserted offline.
  }

  void online_pipeline_tests()
  {
    InMemoryPackageMetadataCache cache;
    auto authorities = build_registry_authority_directory({{"vix", "vix-authority"}});
    assert(authorities.valid());
    BindingVerifier verifier;
    auto raw = observation(10, digestA, xIdentity, {release(xIdentity)});
    auto admission = admit_online_registry_observation(*authorities.value, verifier, std::nullopt,
      xIdentity, raw, cache);
    assert(admission.valid());
    InMemoryTrustedRegistryStateStore trusted;
    auto committed = trusted.compare_and_advance(std::nullopt, *admission.proposedState);
    assert(committed.valid());
    assert(trusted.load("vix").state == admission.proposedState);
    OfflineCachedPackageMetadataSource source(trusted, cache, *authorities.value, verifier);
    NeverExternalSource unusedOnline;
    auto acquired = acquire_resolution_catalog({registry_requirement("x", xIdentity)},
      AcquisitionMode::offline, unusedOnline, source);
    assert(acquired.valid() && acquired.catalog);
    auto graph = resolve_packages({registry_requirement("x", xIdentity)}, *acquired.catalog);
    assert(graph.valid());
    auto lock = make_resolved_lockfile(*graph.value);
    assert(lock.valid() && validate_lockfile_integrity(*lock.value).valid());
    auto serialized = serialize_lockfile(*lock.value);
    assert(serialized.valid());
    assert(parse_lockfile_text(*serialized.text).valid());
  }

  void online_failure_does_not_fall_back_to_cache_test()
  {
    const TrustedRegistrySnapshotState baseline{"vix", 50, digestA};
    InMemoryTrustedRegistryStateStore trusted;
    assert(trusted.compare_and_advance(std::nullopt, baseline).valid());
    auto authorities = build_registry_authority_directory({{"vix", "vix-authority"}});
    auto locations = build_registry_directory({{"vix", {{"online-origin"}}}});
    assert(authorities.valid() && locations.valid());
    BindingVerifier verifier;

    CountingCache cache;
    auto cached = admit_online_registry_observation(*authorities.value, verifier, baseline,
      xIdentity, observation(50, digestA, xIdentity, {release(xIdentity)}), cache.backing);
    assert(cached.valid());

    FixedLocationSource failing;
    failing.value = observation(50, digestA, xIdentity, {});
    failing.fail = true;
    NeverExternalSource gitSource;
    VerifiedRoutedPackageMetadataSource online(*locations.value, *authorities.value,
      failing, verifier, gitSource);
    OfflineCachedPackageMetadataSource offline(trusted, cache, *authorities.value, verifier);
    const auto acquired = acquire_resolution_catalog({registry_requirement("x", xIdentity)},
      AcquisitionMode::online, online, offline);
    assert(!acquired.valid() && !acquired.catalog);
    assert(acquired.issues.size() == 1);
    assert(acquired.issues.front().code == MetadataAcquisitionErrorCode::registry_source_failure);
    assert(acquired.issues.front().message.find("all_registry_locations_untrusted_or_failed") != std::string::npos);
    assert(failing.locations.size() == 1 && failing.locations.front() == "online-origin");
    assert(cache.registryReads == 0 && cache.gitReads == 0);

    FixedLocationSource successful;
    successful.value = observation(50, digestA, xIdentity, {release(xIdentity)});
    VerifiedRoutedPackageMetadataSource routed(*locations.value, *authorities.value,
      successful, verifier, gitSource);
    const auto onlineCatalog = acquire_resolution_catalog({registry_requirement("x", xIdentity)},
      AcquisitionMode::online, routed, offline);
    assert(onlineCatalog.valid() && onlineCatalog.catalog);
    assert(successful.locations == std::vector<std::string>{"online-origin"});
    assert(cache.registryReads == 0 && cache.gitReads == 0);
  }

  void registry_cache_location_invariance_test()
  {
    const RegistryPackageIdentity identity{"vix", "example", "relocatable"};
    const auto raw = observation(50, digestA, identity, {release(identity)});
    auto authorities = build_registry_authority_directory({{"vix", "vix-authority"}});
    auto fromA = build_registry_directory({{"vix", {{"location-A"}}}});
    auto fromB = build_registry_directory({{"vix", {{"location-B"}}}});
    assert(authorities.valid() && fromA.valid() && fromB.valid());
    BindingVerifier verifier;
    FixedLocationSource sourceA;
    FixedLocationSource sourceB;
    sourceA.value = raw;
    sourceB.value = raw;
    auto routedA = route_verified_registry_observation(*fromA.value, *authorities.value,
      sourceA, verifier, identity);
    auto routedB = route_verified_registry_observation(*fromB.value, *authorities.value,
      sourceB, verifier, identity);
    assert(routedA.valid() && routedB.valid());
    assert(sourceA.locations == std::vector<std::string>{"location-A"});
    assert(sourceB.locations == std::vector<std::string>{"location-B"});

    InMemoryPackageMetadataCache cacheA;
    InMemoryPackageMetadataCache cacheB;
    const auto admissionA = admit_online_registry_observation(*authorities.value, verifier,
      std::nullopt, identity, sourceA.value, cacheA);
    const auto admissionB = admit_online_registry_observation(*authorities.value, verifier,
      std::nullopt, identity, sourceB.value, cacheB);
    assert(admissionA.valid() && admissionB.valid());
    // The cache API is addressed only by snapshot + logical package query;
    // location A/B is deliberately not representable in this key.
    auto entryA = cacheA.read_registry({"vix", 50, digestA}, identity);
    auto entryB = cacheB.read_registry({"vix", 50, digestA}, identity);
    assert(entryA.value && entryB.value);
    assert(entryA.value->snapshot == entryB.value->snapshot && entryA.value->query == entryB.value->query);
    assert(entryA.value->releases.size() == entryB.value->releases.size());
    assert(entryA.value->releases.front().contentDigest == entryB.value->releases.front().contentDigest);

    InMemoryTrustedRegistryStateStore trustedA;
    InMemoryTrustedRegistryStateStore trustedB;
    assert(trustedA.compare_and_advance(std::nullopt, *admissionA.proposedState).valid());
    assert(trustedB.compare_and_advance(std::nullopt, *admissionB.proposedState).valid());
    const std::vector<PackageRequirement> roots{registry_requirement("relocatable", identity)};
    const auto lockA = resolve_offline_lock(trustedA, cacheA, *authorities.value, verifier, roots);
    const auto lockB = resolve_offline_lock(trustedB, cacheB, *authorities.value, verifier, roots);
    assert(lockA.lock.roots.size() == 1 && lockB.lock.roots.size() == 1);
    assert(lockA.lock.roots.front().alias == lockB.lock.roots.front().alias);
    assert(lockA.lock.roots.front().node == lockB.lock.roots.front().node);
    assert(std::get<RegistryLockNode>(lockA.lock.nodes.front()).identity ==
      std::get<RegistryLockNode>(lockB.lock.nodes.front()).identity);
    assert(std::get<RegistryLockNode>(lockA.lock.nodes.front()).id ==
      std::get<RegistryLockNode>(lockB.lock.nodes.front()).id);
    assert(lockA.text == lockB.text);
  }

  void mixed_offline_graph_and_transitive_miss_tests()
  {
    InMemoryTrustedRegistryStateStore trusted;
    const TrustedRegistrySnapshotState baseline{"vix", 51, digestA};
    assert(trusted.compare_and_advance(std::nullopt, baseline).valid());
    InMemoryPackageMetadataCache cacheForward;
    InMemoryPackageMetadataCache cacheReverse;
    auto authorities = build_registry_authority_directory({{"vix", "vix-authority"}});
    assert(authorities.valid());
    BindingVerifier verifier;
    const RegistryPackageIdentity a{"vix", "example", "a"};
    const RegistryPackageIdentity c{"vix", "example", "c"};
    const GitPackageIdentity b{"https://example.invalid/org/b.git", true};
    const GitRevisionRequirement tag = TagRevision{"v1"};
    PackageContract gitContract;
    gitContract.identity = GitPackageContractIdentity{};
    gitContract.packages = {registry_requirement("c-local", c)};
    gitContract.integration = NativeVixIntegration{};
    gitContract.exports.push_back({"api", NativeExportMapping{"api"}});
    GitResolution gitResolution{b.normalizedRepository, tag,
      "0123456789abcdef0123456789abcdef01234567", digestB, gitContract};
    PackageContract aContract = contract(a, {1, 0, 0, std::nullopt},
      {{"b-local", GitRequirement{b.normalizedRepository, true, tag}}});
    const auto aRelease = RegistryCandidate{a, {1, 0, 0, std::nullopt}, digestA, aContract};
    const auto cRelease = release(c);
    auto admittedA = admit_online_registry_observation(*authorities.value, verifier, baseline,
      a, observation(51, digestA, a, {aRelease}), cacheForward);
    assert(admittedA.valid());
    assert(cacheForward.write_git(b, tag, gitResolution).valid());
    auto admittedC = admit_online_registry_observation(*authorities.value, verifier, baseline,
      c, observation(51, digestA, c, {cRelease}), cacheForward);
    assert(admittedC.valid());

    // The same semantic records, inserted in reverse order, must yield the
    // identical full offline resolution and persisted lock representation.
    auto admittedCReverse = admit_online_registry_observation(*authorities.value, verifier, baseline,
      c, observation(51, digestA, c, {cRelease}), cacheReverse);
    auto admittedAReverse = admit_online_registry_observation(*authorities.value, verifier, baseline,
      a, observation(51, digestA, a, {aRelease}), cacheReverse);
    assert(admittedCReverse.valid() && admittedAReverse.valid());
    assert(cacheReverse.write_git(b, tag, gitResolution).valid());

    const std::vector<PackageRequirement> roots{registry_requirement("root-a", a)};
    const auto forward = resolve_offline_lock(trusted, cacheForward, *authorities.value, verifier, roots);
    const auto reverse = resolve_offline_lock(trusted, cacheReverse, *authorities.value, verifier, roots);
    assert(forward.lock.roots.size() == reverse.lock.roots.size());
    assert(forward.lock.roots.front().alias == reverse.lock.roots.front().alias);
    assert(forward.lock.roots.front().node == reverse.lock.roots.front().node);
    assert(forward.lock.nodes.size() == reverse.lock.nodes.size());
    for (std::size_t index = 0; index < forward.lock.nodes.size(); ++index)
    {
      const auto *forwardRegistry = std::get_if<RegistryLockNode>(&forward.lock.nodes[index]);
      const auto *reverseRegistry = std::get_if<RegistryLockNode>(&reverse.lock.nodes[index]);
      if (forwardRegistry && reverseRegistry) assert(forwardRegistry->id == reverseRegistry->id);
      else
      {
        const auto *forwardGit = std::get_if<GitLockNode>(&forward.lock.nodes[index]);
        const auto *reverseGit = std::get_if<GitLockNode>(&reverse.lock.nodes[index]);
        assert(forwardGit && reverseGit && forwardGit->id == reverseGit->id);
      }
    }
    assert(forward.text == reverse.text);

    InMemoryPackageMetadataCache partial;
    auto onlyA = admit_online_registry_observation(*authorities.value, verifier, baseline,
      a, observation(51, digestA, a, {aRelease}), partial);
    assert(onlyA.valid());
    OfflineCachedPackageMetadataSource partialOffline(trusted, partial, *authorities.value, verifier);
    auto partialResult = acquire_resolution_catalog({registry_requirement("root-a", a)}, partialOffline);
    assert(!partialResult.valid() && !partialResult.catalog);
    assert(partialResult.issues.size() == 1);
    assert(partialResult.issues.front().code == MetadataAcquisitionErrorCode::git_source_failure);
    assert(partialResult.issues.front().message.find("offline_git_metadata_unavailable") != std::string::npos);
  }

  void cache_order_and_registry_independence_tests()
  {
    auto authorities = build_registry_authority_directory({{"vix", "authority-vix"}, {"acme", "authority-acme"}});
    assert(authorities.valid());
    BindingVerifier verifier;
    const RegistryPackageIdentity vixPackage{"vix", "example", "one"};
    const RegistryPackageIdentity acmePackage{"acme", "example", "two"};
    InMemoryPackageMetadataCache forward;
    InMemoryPackageMetadataCache reverse;
    const auto vixObservation = observation(51, digestA, vixPackage, {release(vixPackage)});
    const auto acmeObservation = observation(8, digestB, acmePackage, {release(acmePackage, digestB)});
    assert(admit_online_registry_observation(*authorities.value, verifier, std::nullopt,
      vixPackage, vixObservation, forward).valid());
    assert(admit_online_registry_observation(*authorities.value, verifier, std::nullopt,
      acmePackage, acmeObservation, forward).valid());
    assert(admit_online_registry_observation(*authorities.value, verifier, std::nullopt,
      acmePackage, acmeObservation, reverse).valid());
    assert(admit_online_registry_observation(*authorities.value, verifier, std::nullopt,
      vixPackage, vixObservation, reverse).valid());
    auto forwardVix = forward.read_registry({"vix", 51, digestA}, vixPackage);
    auto reverseVix = reverse.read_registry({"vix", 51, digestA}, vixPackage);
    auto forwardAcme = forward.read_registry({"acme", 8, digestB}, acmePackage);
    auto reverseAcme = reverse.read_registry({"acme", 8, digestB}, acmePackage);
    assert(forwardVix.value && reverseVix.value && forwardVix.value->releases.front().contentDigest ==
      reverseVix.value->releases.front().contentDigest);
    assert(forwardAcme.value && reverseAcme.value && forwardAcme.value->releases.front().contentDigest ==
      reverseAcme.value->releases.front().contentDigest);

    InMemoryTrustedRegistryStateStore trusted;
    assert(trusted.compare_and_advance(std::nullopt, {"vix", 51, digestA}).valid());
    assert(trusted.compare_and_advance(std::nullopt, {"acme", 8, digestB}).valid());
    OfflineCachedPackageMetadataSource offline(trusted, forward, *authorities.value, verifier);
    assert(offline.registry_releases(vixPackage).succeeded);
    assert(offline.registry_releases(acmePackage).succeeded);
    assert(offline.pins().size() == 2);
  }
}

int main()
{
  trusted_store_tests();
  cache_and_offline_tests();
  no_trust_and_tamper_tests();
  git_offline_tests();
  online_pipeline_tests();
  online_failure_does_not_fall_back_to_cache_test();
  registry_cache_location_invariance_test();
  mixed_offline_graph_and_transitive_miss_tests();
  cache_order_and_registry_independence_tests();
  return 0;
}
