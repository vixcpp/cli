#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/Lockfile.hpp>
#include <vix/cli/project/native/MetadataAcquisition.hpp>
#include <vix/cli/project/native/MetadataStorage.hpp>
#include <vix/cli/project/native/Resolver.hpp>
#include <vix/cli/project/native/TomlParser.hpp>

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using namespace vix::cli::project::native;
namespace fs = std::filesystem;

namespace
{
  constexpr const char *digestA = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  constexpr const char *digestB = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

  struct TempRoot
  {
    fs::path path;
    explicit TempRoot(const std::string &label)
      : path(fs::temp_directory_path() / ("vix-p10m-" + label + "-" + sha256_hex(label)))
    { std::error_code ec; fs::remove_all(path, ec); }
    ~TempRoot() { std::error_code ec; fs::remove_all(path, ec); }
  };

  struct TestVerifier final : RegistryAuthorityVerifier
  {
    bool verify_evidence(const RegistryAuthorityDefinition &, const UnverifiedRegistryObservation &value,
      std::string &diagnostic) const override
    {
      if (value.authorityEvidence == evidence(value)) return true;
      diagnostic = "test observation evidence mismatch";
      return false;
    }
    static std::string evidence(const UnverifiedRegistryObservation &value)
    {
      std::string result = value.snapshot.registryId + ":" + std::to_string(value.snapshot.sequence) + ":" +
        value.snapshot.digest + ":" + value.query.registry + ":" + value.query.publisher + ":" + value.query.name;
      for (const auto &candidate : value.releases)
        result += ":" + candidate.identity.name + ":" + std::to_string(candidate.version.major) + "." +
          std::to_string(candidate.version.minor) + "." + std::to_string(candidate.version.patch) + ":" + candidate.contentDigest;
      for (const auto &candidate : value.releases)
      {
        result += ":contract:" + std::to_string(candidate.contract.format);
        if (const auto *registry = std::get_if<RegistryPackageContractIdentity>(&candidate.contract.identity))
          result += ":registry:" + registry->identity.registry + ":" + registry->identity.publisher + ":" +
            registry->identity.name + ":" + std::to_string(registry->version.major) + "." +
            std::to_string(registry->version.minor) + "." + std::to_string(registry->version.patch);
        else result += ":git";
        for (const auto &requirement : candidate.contract.packages)
        {
          result += ":requirement:" + requirement.alias;
          if (const auto *registry = std::get_if<RegistryRequirement>(&requirement.source))
          {
            result += ":registry:" + registry->identity.registry + ":" + registry->identity.publisher + ":" + registry->identity.name;
            if (const auto *exact = std::get_if<ExactVersion>(&registry->version))
              result += ":exact:" + std::to_string(exact->version.major) + "." + std::to_string(exact->version.minor) + "." + std::to_string(exact->version.patch);
            else
            {
              const auto &version = std::get<CompatibleVersion>(registry->version).version;
              result += ":compatible:" + std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
            }
          }
          else
          {
            const auto &git = std::get<GitRequirement>(requirement.source);
            result += ":git:" + git.normalizedRepository + ":" + std::visit([](const auto &revision) { return revision.value; }, git.revision);
          }
        }
        if (const auto *native = std::get_if<NativeVixIntegration>(&candidate.contract.integration))
          for (const auto &path : native->includeRoots) result += ":include:" + path;
        else result += ":cmake:" + std::get<CMakeConfigIntegration>(candidate.contract.integration).configName;
        for (const auto &exported : candidate.contract.exports)
        {
          result += ":export:" + exported.name;
          if (const auto *native = std::get_if<NativeExportMapping>(&exported.mapping)) result += ":target:" + native->projectTarget;
          else result += ":cmake-target:" + std::get<CMakeExportMapping>(exported.mapping).cmakeTarget;
        }
      }
      return result;
    }
  };

  struct FixedObservationSource final : RegistryObservationLocationSource
  {
    UnverifiedRegistryObservation observation;
    std::vector<std::string> visited;
    UnverifiedRegistryLocationResult registry_observation_at(
      const RegistryLocation &location, const RegistryPackageIdentity &) override
    {
      visited.push_back(location.descriptor);
      return {RegistryObservationLocationStatus::available, observation, {}};
    }
  };

  PackageRequirement registry_requirement(std::string alias, RegistryPackageIdentity identity)
  { return {std::move(alias), RegistryRequirement{std::move(identity), ExactVersion{{1, 0, 0, std::nullopt}}}}; }

  PackageContract registry_contract(const RegistryPackageIdentity &identity, Version version,
    std::vector<PackageRequirement> requirements = {})
  {
    PackageContract result;
    result.identity = RegistryPackageContractIdentity{identity, version};
    result.packages = std::move(requirements);
    result.integration = NativeVixIntegration{{"include"}};
    result.exports.push_back({"api", NativeExportMapping{"api_target"}});
    return result;
  }

  PackageContract git_contract(std::vector<PackageRequirement> requirements = {})
  {
    PackageContract result;
    result.identity = GitPackageContractIdentity{};
    result.packages = std::move(requirements);
    result.integration = CMakeConfigIntegration{"protocol-config"};
    result.exports.push_back({"protocol", CMakeExportMapping{"protocol::protocol"}});
    return result;
  }

  RegistryCandidate candidate(const RegistryPackageIdentity &identity, std::string digest = digestA,
    std::vector<PackageRequirement> requirements = {})
  {
    const Version version{1, 0, 0, std::nullopt};
    return {identity, version, std::move(digest), registry_contract(identity, version, std::move(requirements))};
  }

  UnverifiedRegistryObservation observation(std::uint64_t sequence, std::string snapshotDigest,
    const RegistryPackageIdentity &query, std::vector<RegistryCandidate> candidates)
  {
    UnverifiedRegistryObservation result{{query.registry, sequence, std::move(snapshotDigest)}, query,
      std::move(candidates), {}};
    result.authorityEvidence = TestVerifier::evidence(result);
    return result;
  }

  RegistryAuthorityDirectory authorities()
  {
    auto result = build_registry_authority_directory({{"vix", "vix-test-authority"}, {"acme", "acme-test-authority"}});
    assert(result.valid());
    return std::move(*result.value);
  }

  std::string bytes_at(const fs::path &path)
  {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  }

  void write_bytes(const fs::path &path, const std::string &bytes)
  {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output && (output << bytes));
  }

  fs::path only_record(const fs::path &directory)
  {
    for (const auto &entry : fs::directory_iterator(directory))
      if (entry.path().extension() == ".toml") return entry.path();
    assert(false && "expected cache record");
    return {};
  }

  void persist_observation(FilesystemMetadataStorage &storage, const RegistryAuthorityDirectory &authority,
    const RegistryPackageIdentity &query, const UnverifiedRegistryObservation &raw,
    const std::optional<TrustedRegistrySnapshotState> &previous)
  {
    TestVerifier verifier;
    auto admitted = admit_online_registry_observation(authority, verifier, previous, query, raw, storage);
    assert(admitted.valid() && admitted.proposedState);
  }

  struct Pipeline
  {
    Lockfile lock;
    std::string text;
  };

  struct NeverMetadataSource final : PackageMetadataSource
  {
    unsigned registryCalls{0};
    unsigned gitCalls{0};
    RegistryQueryResult registry_releases(const RegistryPackageIdentity &) override
    { ++registryCalls; return {false, {}, "offline path called online Registry source"}; }
    GitQueryResult resolve_git(const GitPackageIdentity &, const GitRevisionRequirement &) override
    { ++gitCalls; return {GitQueryStatus::failed, std::nullopt, "offline path called online Git source"}; }
  };

  Pipeline offline_pipeline(FilesystemMetadataStorage &storage, const RegistryAuthorityDirectory &authority,
    const std::vector<PackageRequirement> &roots)
  {
    TestVerifier verifier;
    OfflineCachedPackageMetadataSource source(storage, storage, authority, verifier);
    NeverMetadataSource online;
    auto acquired = acquire_resolution_catalog(roots, AcquisitionMode::offline, online, source);
    assert(online.registryCalls == 0 && online.gitCalls == 0);
    assert(acquired.valid() && acquired.catalog);
    auto resolved = resolve_packages(roots, *acquired.catalog);
    assert(resolved.valid());
    auto lock = make_resolved_lockfile(*resolved.value);
    assert(lock.valid() && validate_lockfile_integrity(*lock.value).valid());
    auto serialized = serialize_lockfile(*lock.value);
    assert(serialized.valid() && serialized.text);
    auto reparsed = parse_lockfile_text(*serialized.text);
    assert(reparsed.valid() && reparsed.value);
    return {*lock.value, *serialized.text};
  }

  void trusted_state_tests()
  {
    TempRoot rootA{"trusted-a"};
    TempRoot rootB{"trusted-b"};
    const TrustedRegistrySnapshotState vix50{"vix", 50, digestA};
    const TrustedRegistrySnapshotState vix52{"vix", 52, digestB};
    const TrustedRegistrySnapshotState acme8{"acme", 8, digestB};
    {
      FilesystemMetadataStorage first{rootA.path};
      assert(first.load("vix").valid() && !first.load("vix").state); // absent means empty
      assert(first.compare_and_advance(std::nullopt, vix50).valid());
      assert(first.compare_and_advance(std::nullopt, acme8).valid());
      FilesystemMetadataStorage second{rootA.path};
      assert(first.load("vix").state == vix50);
      assert(second.load("vix").state == vix50 && second.load("acme").state == acme8);
      assert(second.compare_and_advance(vix50, vix52).valid());
      const auto stale = first.compare_and_advance(vix50, TrustedRegistrySnapshotState{"vix", 51, digestB});
      assert(!stale.valid() && stale.issues.front().code == TrustedStateStoreErrorCode::trusted_state_concurrent_update);
      assert(first.load("vix").state == vix52 && first.load("acme").state == acme8);
    }
    {
      FilesystemMetadataStorage first{rootB.path};
      assert(first.compare_and_advance(std::nullopt, acme8).valid());
      assert(first.compare_and_advance(std::nullopt, vix50).valid());
    }
    // Insertion-order determinism is checked using equal maps written in
    // opposite orders.
    TempRoot orderA{"state-order-a"};
    TempRoot orderB{"state-order-b"};
    { FilesystemMetadataStorage store{orderA.path}; assert(store.compare_and_advance(std::nullopt, vix50).valid()); assert(store.compare_and_advance(std::nullopt, acme8).valid()); }
    { FilesystemMetadataStorage store{orderB.path}; assert(store.compare_and_advance(std::nullopt, acme8).valid()); assert(store.compare_and_advance(std::nullopt, vix50).valid()); }
    assert(bytes_at(orderA.path / "trusted" / "state.toml") == bytes_at(orderB.path / "trusted" / "state.toml"));

    const auto trustedDir = orderA.path / "trusted";
    write_bytes(trustedDir / "state.toml.tmp", "not authoritative");
    { FilesystemMetadataStorage reopened{orderA.path}; assert(reopened.load("vix").state == vix50); }
    write_bytes(trustedDir / "state.toml", "format = 999\nregistries = []\n");
    { FilesystemMetadataStorage corrupt{orderA.path}; const auto result = corrupt.load("vix");
      assert(!result.valid() && result.issues.front().code == TrustedStateStoreErrorCode::trusted_state_storage_corrupt); }
    write_bytes(trustedDir / "state.toml", "format = 1\nregistries = [{ id = \"vix\", sequence = 0, digest = \"bad\" }]\n");
    { FilesystemMetadataStorage corrupt{orderA.path}; const auto result = corrupt.load("vix");
      assert(!result.valid() && result.issues.front().code == TrustedStateStoreErrorCode::trusted_state_storage_corrupt); }
    write_bytes(trustedDir / "state.toml", "format = 1\nregistries = [{ id = \"vix\", sequence = 50, digest = \"" + std::string{digestA} + "\" }, { id = \"vix\", sequence = 51, digest = \"" + digestB + "\" }]\n");
    { FilesystemMetadataStorage corrupt{orderA.path}; const auto result = corrupt.load("vix");
      assert(!result.valid() && result.issues.front().code == TrustedStateStoreErrorCode::trusted_state_storage_corrupt); }
    write_bytes(trustedDir / "state.toml", "format = 1\nunknown = true\nregistries = []\n");
    { FilesystemMetadataStorage corrupt{orderA.path}; const auto result = corrupt.load("vix");
      assert(!result.valid() && result.issues.front().code == TrustedStateStoreErrorCode::trusted_state_storage_corrupt); }
  }

  void registry_cache_tests()
  {
    TempRoot root{"registry-cache"};
    FilesystemMetadataStorage storage{root.path};
    auto authority = authorities();
    TestVerifier verifier;
    const RegistryPackageIdentity x{"vix", "example", "x"};
    const TrustedRegistrySnapshotState state{"vix", 51, digestA};
    const auto raw = observation(51, digestA, x, {candidate(x)});
    auto inserted = admit_online_registry_observation(authority, verifier, state, x, raw, storage);
    assert(inserted.valid());
    const auto firstBytes = bytes_at(only_record(root.path / "metadata" / "registry"));
    FilesystemMetadataStorage reopened{root.path};
    auto replay = admit_online_registry_observation(authority, verifier, state, x, raw, reopened);
    assert(replay.valid()); // identical replay is idempotent across instances
    assert(firstBytes == bytes_at(only_record(root.path / "metadata" / "registry")));
    auto loaded = reopened.read_registry({"vix", 51, digestA}, x);
    assert(loaded.valid() && loaded.value->authorityEvidence == raw.authorityEvidence);
    assert(loaded.value->releases.size() == 1 && loaded.value->releases.front().version == raw.releases.front().version);
    assert(loaded.value->releases.front().contract.packages.empty());
    assert(std::get<NativeVixIntegration>(loaded.value->releases.front().contract.integration).includeRoots == std::vector<std::string>{"include"});

    const auto contradictory = observation(51, digestA, x, {candidate(x, digestB)});
    auto conflict = admit_online_registry_observation(authority, verifier, state, x, contradictory, reopened);
    assert(!conflict.valid() && conflict.issues.front().code == MetadataCacheErrorCode::cache_conflict);
    assert(std::distance(fs::directory_iterator(root.path / "metadata" / "registry"), fs::directory_iterator{}) == 1);

    const auto target = only_record(root.path / "metadata" / "registry");
    write_bytes(target.string() + ".tmp", "malformed orphan");
    assert(reopened.read_registry({"vix", 51, digestA}, x).valid());
    write_bytes(target, "format = 999\nkind = \"registry\"\n");
    auto unknown = reopened.read_registry({"vix", 51, digestA}, x);
    assert(!unknown.valid() && unknown.issues.front().code == MetadataCacheErrorCode::metadata_cache_corrupt);
    write_bytes(target, "truncated = [\n");
    auto corrupt = reopened.read_registry({"vix", 51, digestA}, x);
    assert(!corrupt.valid() && corrupt.issues.front().code == MetadataCacheErrorCode::metadata_cache_corrupt);
  }

  void git_cache_tests()
  {
    TempRoot root{"git-cache"};
    FilesystemMetadataStorage storage{root.path};
    GitPackageIdentity identity{"https://example.invalid/org/repo.git", true};
    GitRevisionRequirement revision = BranchRevision{"main"};
    GitResolution resolution{identity.normalizedRepository, revision,
      "0123456789abcdef0123456789abcdef01234567", digestB, git_contract()};
    assert(storage.write_git(identity, revision, resolution).valid());
    FilesystemMetadataStorage reopened{root.path};
    assert(reopened.write_git(identity, revision, resolution).valid());
    auto loaded = reopened.read_git(identity, revision);
    assert(loaded.valid() && loaded.value->commit == resolution.commit);
    assert(std::get<CMakeConfigIntegration>(loaded.value->contract.integration).configName == "protocol-config");
    auto conflict = resolution;
    conflict.commit = "abcdef0123456789abcdef0123456789abcdef01";
    assert(!reopened.write_git(identity, revision, conflict).valid());
    const auto target = only_record(root.path / "metadata" / "git");
    write_bytes(target.string() + ".tmp", "ignored");
    assert(reopened.read_git(identity, revision).valid());
    write_bytes(target, "format = 100\nkind = \"git\"\n");
    auto unknown = reopened.read_git(identity, revision);
    assert(!unknown.valid() && unknown.issues.front().code == MetadataCacheErrorCode::metadata_cache_corrupt);
  }

  void offline_policy_and_clear_tests()
  {
    TempRoot root{"offline-policy"};
    auto authority = authorities();
    TestVerifier verifier;
    const RegistryPackageIdentity x{"vix", "example", "x"};
    const TrustedRegistrySnapshotState oldState{"vix", 50, digestA};
    const TrustedRegistrySnapshotState newState{"vix", 51, digestB};
    {
      FilesystemMetadataStorage store{root.path};
      assert(store.compare_and_advance(std::nullopt, oldState).valid());
      persist_observation(store, authority, x, observation(50, digestA, x, {candidate(x, digestA)}), oldState);
      persist_observation(store, authority, x, observation(51, digestB, x, {candidate(x, digestB)}), oldState);
      GitPackageIdentity git{"https://example.invalid/org/offline.git", true};
      GitRevisionRequirement tag = TagRevision{"v1"};
      GitResolution gitValue{git.normalizedRepository, tag, "0123456789abcdef0123456789abcdef01234567", digestB, git_contract()};
      assert(store.write_git(git, tag, gitValue).valid());
      OfflineCachedPackageMetadataSource offline(store, store, authority, verifier);
      auto selected = offline.registry_releases(x);
      assert(selected.succeeded && selected.releases.front().contentDigest == digestA);
      assert(store.load("vix").state == oldState); // a newer cache record never advances trust
      assert(store.clear_cache().valid());
      assert(store.load("vix").state == oldState);
      assert(!offline.registry_releases(x).succeeded);
      assert(!store.read_git(git, tag).value);
    }

    TempRoot olderRoot{"older-cache"};
    FilesystemMetadataStorage older{olderRoot.path};
    assert(older.compare_and_advance(std::nullopt, newState).valid());
    persist_observation(older, authority, x, observation(50, digestA, x, {candidate(x)}), oldState);
    OfflineCachedPackageMetadataSource olderOffline(older, older, authority, verifier);
    auto miss = olderOffline.registry_releases(x);
    assert(!miss.succeeded && olderOffline.last_issues().front().code == MetadataCacheErrorCode::offline_registry_metadata_unavailable);

    TempRoot noTrustRoot{"no-trust-cache"};
    FilesystemMetadataStorage noTrust{noTrustRoot.path};
    persist_observation(noTrust, authority, x, observation(50, digestA, x, {candidate(x)}), std::nullopt);
    OfflineCachedPackageMetadataSource noTrustOffline(noTrust, noTrust, authority, verifier);
    auto unavailable = noTrustOffline.registry_releases(x);
    assert(!unavailable.succeeded && noTrustOffline.last_issues().front().code == MetadataCacheErrorCode::offline_trusted_state_unavailable);

    TempRoot emptyRoot{"empty-versus-miss"};
    FilesystemMetadataStorage emptyStore{emptyRoot.path};
    assert(emptyStore.compare_and_advance(std::nullopt, oldState).valid());
    const RegistryPackageIdentity emptyIdentity{"vix", "example", "empty"};
    persist_observation(emptyStore, authority, emptyIdentity,
      observation(50, digestA, emptyIdentity, {}), oldState);
    OfflineCachedPackageMetadataSource emptyOffline(emptyStore, emptyStore, authority, verifier);
    const auto presentEmpty = emptyOffline.registry_releases(emptyIdentity);
    assert(presentEmpty.succeeded && presentEmpty.releases.empty());
    const RegistryPackageIdentity absentQuery{"vix", "example", "not-cached"};
    const auto absent = emptyOffline.registry_releases(absentQuery);
    assert(!absent.succeeded && emptyOffline.last_issues().front().code == MetadataCacheErrorCode::offline_registry_metadata_unavailable);
    auto emptyCatalog = acquire_resolution_catalog({registry_requirement("empty", emptyIdentity)}, emptyOffline);
    assert(emptyCatalog.valid() && emptyCatalog.catalog);
    const auto absentResolution = resolve_packages({registry_requirement("empty", emptyIdentity)}, *emptyCatalog.catalog);
    assert(!absentResolution.valid() && absentResolution.issues.front().code == ResolverErrorCode::no_candidate);
  }

  void disk_location_invariance_test()
  {
    TempRoot rootA{"disk-location-a"};
    TempRoot rootB{"disk-location-b"};
    auto authority = authorities();
    TestVerifier verifier;
    const RegistryPackageIdentity package{"vix", "example", "relocated"};
    const TrustedRegistrySnapshotState trusted{"vix", 50, digestA};
    auto locationsA = build_registry_directory({{"vix", {{"mirror-A"}}}});
    auto locationsB = build_registry_directory({{"vix", {{"mirror-B"}}}});
    assert(locationsA.valid() && locationsB.valid());
    FixedObservationSource sourceA;
    FixedObservationSource sourceB;
    sourceA.observation = observation(50, digestA, package, {candidate(package)});
    sourceB.observation = observation(50, digestA, package, {candidate(package)});
    auto routedA = route_verified_registry_observation(*locationsA.value, authority, sourceA, verifier, package);
    auto routedB = route_verified_registry_observation(*locationsB.value, authority, sourceB, verifier, package);
    assert(routedA.valid() && routedB.valid());
    assert(sourceA.visited == std::vector<std::string>{"mirror-A"});
    assert(sourceB.visited == std::vector<std::string>{"mirror-B"});
    // The cache API receives neither routing descriptor.
    {
      FilesystemMetadataStorage store{rootA.path};
      assert(store.compare_and_advance(std::nullopt, trusted).valid());
      persist_observation(store, authority, package, sourceA.observation, trusted);
    }
    {
      FilesystemMetadataStorage store{rootB.path};
      assert(store.compare_and_advance(std::nullopt, trusted).valid());
      persist_observation(store, authority, package, sourceB.observation, trusted);
    }
    const auto recordA = only_record(rootA.path / "metadata" / "registry");
    const auto recordB = only_record(rootB.path / "metadata" / "registry");
    assert(recordA.filename() == recordB.filename());
    assert(bytes_at(recordA) == bytes_at(recordB));
    const std::vector<PackageRequirement> roots{registry_requirement("relocated", package)};
    FilesystemMetadataStorage reopenedA{rootA.path};
    FilesystemMetadataStorage reopenedB{rootB.path};
    const auto lockA = offline_pipeline(reopenedA, authority, roots);
    const auto lockB = offline_pipeline(reopenedB, authority, roots);
    assert(lockA.lock.roots.front().node == lockB.lock.roots.front().node);
    assert(std::get<RegistryLockNode>(lockA.lock.nodes.front()).identity ==
      std::get<RegistryLockNode>(lockB.lock.nodes.front()).identity);
    assert(lockA.text == lockB.text);
  }

  void location_invariant_and_restart_pipeline_tests()
  {
    TempRoot firstRoot{"mixed-cache-order-a"};
    TempRoot secondRoot{"mixed-cache-order-b"};
    auto authority = authorities();
    TestVerifier verifier;
    const TrustedRegistrySnapshotState baseline{"vix", 51, digestA};
    const RegistryPackageIdentity a{"vix", "example", "a"};
    const RegistryPackageIdentity c{"vix", "example", "c"};
    const GitPackageIdentity b{"https://example.invalid/org/b.git", true};
    const GitRevisionRequirement tag = TagRevision{"v1"};
    const auto cCandidate = candidate(c);
    PackageContract bContract = git_contract({registry_requirement("c-local", c)});
    GitResolution bResolution{b.normalizedRepository, tag,
      "0123456789abcdef0123456789abcdef01234567", digestB, bContract};
    PackageContract aContract = registry_contract(a, {1, 0, 0, std::nullopt},
      {{"b-local", GitRequirement{b.normalizedRepository, true, tag}}});
    const RegistryCandidate aCandidate{a, {1, 0, 0, std::nullopt}, digestA, aContract};
    {
      FilesystemMetadataStorage store{firstRoot.path};
      assert(store.compare_and_advance(std::nullopt, baseline).valid());
      persist_observation(store, authority, a, observation(51, digestA, a, {aCandidate}), baseline); // A
      assert(store.write_git(b, tag, bResolution).valid()); // B
      persist_observation(store, authority, c, observation(51, digestA, c, {cCandidate}), baseline); // C
    }
    {
      FilesystemMetadataStorage store{secondRoot.path};
      assert(store.compare_and_advance(std::nullopt, baseline).valid());
      persist_observation(store, authority, c, observation(51, digestA, c, {cCandidate}), baseline); // C
      persist_observation(store, authority, a, observation(51, digestA, a, {aCandidate}), baseline); // A
      assert(store.write_git(b, tag, bResolution).valid()); // B
    }
    const std::vector<PackageRequirement> roots{registry_requirement("root-a", a)};
    Pipeline first;
    Pipeline second;
    {
      FilesystemMetadataStorage reopened{firstRoot.path};
      first = offline_pipeline(reopened, authority, roots);
    }
    {
      FilesystemMetadataStorage reopened{secondRoot.path};
      second = offline_pipeline(reopened, authority, roots);
    }
    assert(first.lock.roots.size() == second.lock.roots.size());
    assert(first.lock.roots.front().alias == second.lock.roots.front().alias);
    assert(first.lock.roots.front().node == second.lock.roots.front().node);
    assert(first.lock.nodes.size() == second.lock.nodes.size());
    assert(first.text == second.text);
  }
}

int main()
{
  trusted_state_tests();
  registry_cache_tests();
  git_cache_tests();
  offline_policy_and_clear_tests();
  disk_location_invariance_test();
  location_invariant_and_restart_pipeline_tests();
  return 0;
}
