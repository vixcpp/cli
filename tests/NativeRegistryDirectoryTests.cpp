#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/Lockfile.hpp>
#include <vix/cli/project/native/MetadataAcquisition.hpp>
#include <vix/cli/project/native/RegistryDirectory.hpp>
#include <vix/cli/project/native/RegistryAuthority.hpp>
#include <vix/cli/project/native/Resolver.hpp>
#include <vix/cli/project/native/Validation.hpp>

#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace vix::cli::project::native;

namespace
{
  static_assert(!std::is_same_v<RegistryLocation, RegistryContentLocation>);
  static_assert(!std::is_convertible_v<RegistryLocation, RegistryContentLocation>);
  static_assert(!std::is_convertible_v<RegistryContentLocation, RegistryLocation>);
  static_assert(std::is_same_v<decltype(&RegistryObservationLocationSource::registry_observation_at),
    UnverifiedRegistryLocationResult (RegistryObservationLocationSource::*)(
      const RegistryLocation &, const RegistryPackageIdentity &)>);

  constexpr const char *digest_a = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  constexpr const char *commit_git = "0123456789abcdef0123456789abcdef01234567";

  PackageRequirement exact(std::string alias, RegistryPackageIdentity identity, Version version)
  {
    return {std::move(alias), RegistryRequirement{std::move(identity), ExactVersion{std::move(version)}}};
  }

  PackageRequirement git_req(std::string alias, std::string repository, GitRevisionRequirement revision)
  {
    return {std::move(alias), GitRequirement{std::move(repository), true, std::move(revision)}};
  }

  PackageContract registry_contract(const RegistryPackageIdentity &identity, Version version)
  {
    PackageContract contract;
    contract.identity = RegistryPackageContractIdentity{identity, std::move(version)};
    contract.integration = NativeVixIntegration{};
    contract.exports.push_back({"api", NativeExportMapping{"api"}});
    return contract;
  }

  PackageContract git_contract()
  {
    PackageContract contract;
    contract.identity = GitPackageContractIdentity{};
    contract.integration = NativeVixIntegration{};
    contract.exports.push_back({"api", NativeExportMapping{"api"}});
    return contract;
  }

  RegistryCandidate release(const RegistryPackageIdentity &identity, Version version)
  {
    auto contract = registry_contract(identity, version);
    return {identity, std::move(version), digest_a, std::move(contract)};
  }

  UnverifiedRegistryObservation make_observation(const RegistryPackageIdentity &identity,
      std::vector<RegistryCandidate> releases)
  {
    UnverifiedRegistryObservation value{{identity.registry, 1, digest_a}, identity, std::move(releases), {}};
    value.authorityEvidence = identity.registry + ":" + identity.publisher + ":" + identity.name + ":" + digest_a;
    for (const auto &item : value.releases)
      value.authorityEvidence += ":" + std::to_string(item.version.major) + "." +
        std::to_string(item.version.minor) + "." + std::to_string(item.version.patch) + ":" + item.contentDigest;
    return value;
  }

  struct TestVerifier final : RegistryAuthorityVerifier
  {
    bool verify_evidence(const RegistryAuthorityDefinition &authority,
        const UnverifiedRegistryObservation &value, std::string &diagnostic) const override
    {
      std::string expected = value.query.registry + ":" + value.query.publisher + ":" + value.query.name + ":" +
        value.snapshot.digest;
      for (const auto &item : value.releases)
        expected += ":" + std::to_string(item.version.major) + "." + std::to_string(item.version.minor) + "." +
          std::to_string(item.version.patch) + ":" + item.contentDigest;
      if (authority.authorityId == "test-authority" && value.authorityEvidence == expected) return true;
      diagnostic = "invalid test evidence";
      return false;
    }
  };

  RegistryAuthorityDirectory authority_directory()
  {
    auto result = build_registry_authority_directory({{"vix", "test-authority"}, {"company", "test-authority"}});
    assert(result.valid());
    return std::move(*result.value);
  }

  bool same_revision(const GitRevisionRequirement &left, const GitRevisionRequirement &right)
  {
    if (left.index() != right.index()) return false;
    return std::visit([&](const auto &revision)
    {
      using Revision = std::decay_t<decltype(revision)>;
      const auto *other = std::get_if<Revision>(&right);
      return other && revision.value == other->value;
    }, left);
  }

  struct LocationReply
  {
    std::string location;
    RegistryPackageIdentity identity;
    UnverifiedRegistryLocationResult result;
  };

  struct MemoryLocations final : RegistryObservationLocationSource
  {
    std::vector<LocationReply> replies;
    std::vector<std::pair<std::string, RegistryPackageIdentity>> calls;

    UnverifiedRegistryLocationResult registry_observation_at(const RegistryLocation &location,
                                              const RegistryPackageIdentity &identity) override
    {
      calls.emplace_back(location.descriptor, identity);
      for (const auto &reply : replies)
        if (reply.location == location.descriptor && reply.identity == identity) return reply.result;
      return {RegistryObservationLocationStatus::not_found, std::nullopt, "no in-memory observation configured"};
    }
  };

  struct GitOnlyMemorySource final : PackageMetadataSource
  {
    std::vector<GitResolution> records;
    RegistryQueryResult registry_releases(const RegistryPackageIdentity &) override
    {
      return {false, {}, "Registry queries must use the routing adapter"};
    }
    GitQueryResult resolve_git(const GitPackageIdentity &identity, const GitRevisionRequirement &revision) override
    {
      for (const auto &record : records)
        if (record.normalizedRepository == identity.normalizedRepository && same_revision(record.requestedRevision, revision))
          return {GitQueryStatus::found, record, {}};
      return {GitQueryStatus::not_found, std::nullopt, {}};
    }
  };

  RegistryDirectory directory(std::string registryId, std::vector<std::string> locations)
  {
    RegistryDefinition definition;
    definition.id = std::move(registryId);
    for (auto &location : locations) definition.locations.push_back({std::move(location)});
    auto result = build_registry_directory({std::move(definition)});
    assert(result.valid());
    return std::move(*result.value);
  }

  bool locks_equal(const Lockfile &left, const Lockfile &right)
  {
    const auto leftText = serialize_lockfile(left);
    const auto rightText = serialize_lockfile(right);
    return leftText.valid() && rightText.valid() && leftText.text == rightText.text;
  }

  Lockfile resolve_through(RegistryDirectory &directory, MemoryLocations &locations,
                           const PackageRequirement &requirement)
  {
    GitOnlyMemorySource git;
    TestVerifier verifier;
    auto authorities = authority_directory();
    VerifiedRoutedPackageMetadataSource routed(directory, authorities, locations, verifier, git);
    const auto acquired = acquire_resolution_catalog({requirement}, routed);
    assert(acquired.valid());
    const auto graph = resolve_packages({requirement}, *acquired.catalog);
    assert(graph.valid());
    const auto lock = make_resolved_lockfile(*graph.value);
    assert(lock.valid() && validate_lockfile_integrity(*lock.value).valid());
    return *lock.value;
  }

  void directory_identity_and_validation_tests()
  {
    const RegistryPackageIdentity logical{"vix", "softadastra", "sqlite"};
    auto first = build_registry_directory({{"vix", {{"location-A"}, {"location-B"}}}});
    auto relocated = build_registry_directory({{"vix", {{"location-C"}}}});
    assert(first.valid() && relocated.valid());
    assert(first.value->find(logical.registry)->id == relocated.value->find(logical.registry)->id);
    assert(logical.registry == "vix" && logical.publisher == "softadastra" && logical.name == "sqlite");

    auto sharedLocation = build_registry_directory({{"vix", {{"location-X"}}}, {"company", {{"location-X"}}}});
    assert(sharedLocation.valid());
    assert(sharedLocation.value->find("vix") && sharedLocation.value->find("company"));
    assert(sharedLocation.value->find("vix")->locations.front() == sharedLocation.value->find("company")->locations.front());
    assert((RegistryPackageIdentity{"vix", "example", "pkg"} !=
            RegistryPackageIdentity{"company", "example", "pkg"}));

    auto duplicateIds = build_registry_directory({{"vix", {{"A"}}}, {"vix", {{"B"}}}});
    assert(!duplicateIds.valid() && duplicateIds.issues.front().code == RegistryDirectoryErrorCode::duplicate_registry_id);
    auto noLocations = build_registry_directory({{"vix", {}}});
    assert(!noLocations.valid() && noLocations.issues.front().code == RegistryDirectoryErrorCode::no_registry_locations);

    auto orderOne = build_registry_directory({{"company", {{"X"}}, {}}, {"vix", {{"A"}, {"B"}}, {}}});
    auto orderTwo = build_registry_directory({{"vix", {{"A"}, {"B"}}, {}}, {"company", {{"X"}}, {}}});
    assert(orderOne.valid() && orderTwo.valid());
    assert(orderOne.value->find("vix")->locations == orderTwo.value->find("vix")->locations);

    RegistryDefinition separated;
    separated.id = "separated";
    separated.locations = {{"metadata-A"}, {"metadata-B"}};
    separated.contentLocations = {{"content-Z"}, {"content-A"}, {"content-M"}};
    auto distinctRouting = build_registry_directory({separated});
    assert(distinctRouting.valid());
    const auto *configured = distinctRouting.value->find("separated");
    assert(configured->locations == separated.locations);
    assert((configured->contentLocations == std::vector<RegistryContentLocation>{
      {"content-Z"}, {"content-A"}, {"content-M"}}));

    RegistryDefinition metadataRoute;
    metadataRoute.id = "vix";
    metadataRoute.locations = {{"metadata-first"}, {"metadata-second"}};
    metadataRoute.contentLocations = {{"content-only"}};
    auto metadataAndContent = build_registry_directory({metadataRoute});
    assert(metadataAndContent.valid());
    MemoryLocations metadataSource;
    metadataSource.replies.push_back({"metadata-first", logical,
      {RegistryObservationLocationStatus::transport_failure, std::nullopt, "first unavailable"}});
    metadataSource.replies.push_back({"metadata-second", logical,
      {RegistryObservationLocationStatus::available, make_observation(logical, {}), {}}});
    TestVerifier metadataVerifier;
    const auto metadataResult = route_verified_registry_observation(
      *metadataAndContent.value, authority_directory(), metadataSource, metadataVerifier, logical);
    assert(metadataResult.valid());
    assert((metadataSource.calls == std::vector<std::pair<std::string, RegistryPackageIdentity>>{
      {"metadata-first", logical}, {"metadata-second", logical}}));

    RegistryDefinition metadataOnly;
    metadataOnly.id = "metadata-only";
    metadataOnly.locations = {{"metadata"}};
    auto noContent = build_registry_directory({metadataOnly});
    assert(noContent.valid());
    assert(noContent.value->find("metadata-only")->contentLocations.empty());

    RegistryDefinition emptyContentLocation;
    emptyContentLocation.id = "empty-content-location";
    emptyContentLocation.locations = {{"metadata"}};
    emptyContentLocation.contentLocations = {{""}};
    auto invalidContentLocation = build_registry_directory({emptyContentLocation});
    assert(!invalidContentLocation.valid());
    assert(invalidContentLocation.issues.front().code == RegistryDirectoryErrorCode::invalid_content_location);

    RegistryDefinition duplicateContentLocations;
    duplicateContentLocations.id = "duplicate-content-location";
    duplicateContentLocations.locations = {{"metadata"}};
    duplicateContentLocations.contentLocations = {{"content"}, {"content"}};
    auto duplicateContentLocation = build_registry_directory({duplicateContentLocations});
    assert(!duplicateContentLocation.valid());
    assert(duplicateContentLocation.issues.front().code == RegistryDirectoryErrorCode::duplicate_content_location);
  }

  void fallback_empty_and_no_merge_tests()
  {
    const RegistryPackageIdentity identity{"vix", "example", "pkg"};
    const auto versionOne = release(identity, {1, 0, 0, std::nullopt});
    const auto versionTwo = release(identity, {2, 0, 0, std::nullopt});

    auto routedDirectory = directory("vix", {"location-A", "location-B"});
    MemoryLocations fallback;
    fallback.replies.push_back({"location-A", identity,
      {RegistryObservationLocationStatus::not_found, std::nullopt, "A unavailable"}});
    fallback.replies.push_back({"location-B", identity,
      {RegistryObservationLocationStatus::available, make_observation(identity, {versionOne}), {}}});
    TestVerifier verifier;
    auto authorities = authority_directory();
    const auto result = route_verified_registry_observation(routedDirectory, authorities, fallback, verifier, identity);
    assert(result.valid() && result.observation->releases().size() == 1);
    assert(fallback.calls.size() == 2 && fallback.calls[0].first == "location-A" && fallback.calls[1].first == "location-B");

    MemoryLocations noMerge;
    noMerge.replies.push_back({"location-A", identity, {RegistryObservationLocationStatus::available, make_observation(identity, {versionOne}), {}}});
    noMerge.replies.push_back({"location-B", identity, {RegistryObservationLocationStatus::available, make_observation(identity, {versionTwo}), {}}});
    const auto routedObservation = route_verified_registry_observation(routedDirectory, authorities, noMerge, verifier, identity);
    assert(routedObservation.valid() && routedObservation.observation->releases().size() == 1);
    assert(routedObservation.observation->releases().front().version == versionOne.version);
    assert(noMerge.calls.size() == 1 && noMerge.calls.front().first == "location-A");

    auto emptyDirectory = directory("vix", {"empty", "would-have-release"});
    MemoryLocations empty;
    empty.replies.push_back({"empty", identity, {RegistryObservationLocationStatus::available, make_observation(identity, {}), {}}});
    empty.replies.push_back({"would-have-release", identity, {RegistryObservationLocationStatus::available, make_observation(identity, {versionOne}), {}}});
    const auto emptyObservation = route_verified_registry_observation(emptyDirectory, authorities, empty, verifier, identity);
    assert(emptyObservation.valid() && emptyObservation.observation->releases().empty());
    assert(empty.calls.size() == 1);
    GitOnlyMemorySource git;
    VerifiedRoutedPackageMetadataSource routed(emptyDirectory, authorities, empty, verifier, git);
    const auto acquired = acquire_resolution_catalog({exact("pkg", identity, {1, 0, 0, std::nullopt})}, routed);
    assert(acquired.valid());
    const auto unresolved = resolve_packages({exact("pkg", identity, {1, 0, 0, std::nullopt})}, *acquired.catalog);
    assert(!unresolved.valid() && unresolved.issues.front().code == ResolverErrorCode::no_candidate);
  }

  void unknown_and_failure_tests()
  {
    auto known = directory("vix", {"A", "B"});
    MemoryLocations source;
    const RegistryPackageIdentity unknown{"missing", "example", "pkg"};
    TestVerifier verifier;
    auto authorities = authority_directory();
    const auto unknownResult = route_verified_registry_observation(known, authorities, source, verifier, unknown);
    assert(!unknownResult.valid() && unknownResult.issues.front().code == VerifiedRegistryRoutingErrorCode::unknown_registry);
    assert(source.calls.empty());

    const RegistryPackageIdentity identity{"vix", "example", "pkg"};
    source.replies = {{"A", identity, {RegistryObservationLocationStatus::transport_failure, std::nullopt, "offline"}},
      {"B", identity, {RegistryObservationLocationStatus::transport_failure, std::nullopt, "unavailable"}}};
    const auto failed = route_verified_registry_observation(known, authorities, source, verifier, identity);
    assert(!failed.valid());
    const auto locationFailures = std::count_if(failed.issues.begin(), failed.issues.end(), [](const auto &issue)
      { return issue.code == VerifiedRegistryRoutingErrorCode::location_or_verification_failure; });
    const auto terminalFailures = std::count_if(failed.issues.begin(), failed.issues.end(), [](const auto &issue)
      { return issue.code == VerifiedRegistryRoutingErrorCode::all_registry_locations_untrusted_or_failed; });
    assert(locationFailures == 2 && terminalFailures == 1);
    assert(std::any_of(failed.issues.begin(), failed.issues.end(), [](const auto &issue) { return issue.location == "A"; }));
    assert(std::any_of(failed.issues.begin(), failed.issues.end(), [](const auto &issue) { return issue.location == "B"; }));

    GitOnlyMemorySource git;
    VerifiedRoutedPackageMetadataSource routed(known, authorities, source, verifier, git);
    const auto acquisition = acquire_resolution_catalog({exact("pkg", identity, {1, 0, 0, std::nullopt})}, routed);
    assert(!acquisition.valid());
    assert(acquisition.issues.front().code == MetadataAcquisitionErrorCode::registry_source_failure);
    assert(acquisition.issues.front().message.find("registry_authentication_or_location_failure") != std::string::npos);
    assert(acquisition.issues.front().message.find("all_registry_locations_untrusted_or_failed") != std::string::npos);
    assert(acquisition.issues.front().message.find("no_candidate") == std::string::npos);
  }

  void relocation_and_mirror_order_lock_invariance_tests()
  {
    const RegistryPackageIdentity identity{"vix", "softadastra", "sqlite"};
    const auto candidate = release(identity, {1, 2, 0, std::nullopt});
    auto directoryA = directory("vix", {"https://host-a.invalid/registry"});
    auto directoryB = directory("vix", {"https://host-b.invalid/registry"});
    MemoryLocations sourceA;
    sourceA.replies.push_back({"https://host-a.invalid/registry", identity, {RegistryObservationLocationStatus::available, make_observation(identity, {candidate}), {}}});
    MemoryLocations sourceB;
    sourceB.replies.push_back({"https://host-b.invalid/registry", identity, {RegistryObservationLocationStatus::available, make_observation(identity, {candidate}), {}}});
    const auto lockA = resolve_through(directoryA, sourceA, exact("db", identity, {1, 2, 0, std::nullopt}));
    const auto lockB = resolve_through(directoryB, sourceB, exact("db", identity, {1, 2, 0, std::nullopt}));
    assert(locks_equal(lockA, lockB));
    assert(std::get<RegistryLockNode>(lockA.nodes.front()).id == std::get<RegistryLockNode>(lockB.nodes.front()).id);
    assert(std::get<RegistryLockNode>(lockA.nodes.front()).identity == std::get<RegistryLockNode>(lockB.nodes.front()).identity);

    auto mirrorsAB = directory("vix", {"A", "B"});
    auto mirrorsBA = directory("vix", {"B", "A"});
    MemoryLocations sourceAB;
    sourceAB.replies = {{"A", identity, {RegistryObservationLocationStatus::available, make_observation(identity, {candidate}), {}}},
      {"B", identity, {RegistryObservationLocationStatus::available, make_observation(identity, {candidate}), {}}}};
    MemoryLocations sourceBA;
    sourceBA.replies = sourceAB.replies;
    TestVerifier verifier;
    auto authorities = authority_directory();
    const auto routeAB = route_verified_registry_observation(mirrorsAB, authorities, sourceAB, verifier, identity);
    const auto routeBA = route_verified_registry_observation(mirrorsBA, authorities, sourceBA, verifier, identity);
    assert(routeAB.valid() && routeBA.valid());
    assert(sourceAB.calls.front().first == "A" && sourceBA.calls.front().first == "B");
    const auto mirrorLockAB = resolve_through(mirrorsAB, sourceAB, exact("db", identity, {1, 2, 0, std::nullopt}));
    const auto mirrorLockBA = resolve_through(mirrorsBA, sourceBA, exact("db", identity, {1, 2, 0, std::nullopt}));
    assert(locks_equal(mirrorLockAB, mirrorLockBA));
  }

  void registry_vs_git_identity_test()
  {
    const RegistryPackageIdentity registryIdentity{"vix", "example", "pkg"};
    constexpr const char *gitRepository = "git://metadata-host.invalid/pkg.git";
    auto registryDirectory = directory("vix", {"git://metadata-host.invalid/registry"});
    MemoryLocations locationSource;
    locationSource.replies.push_back({"git://metadata-host.invalid/registry", registryIdentity,
      {RegistryObservationLocationStatus::available, make_observation(registryIdentity, {release(registryIdentity, {1, 0, 0, std::nullopt})}), {}}});
    GitOnlyMemorySource gitSource;
    gitSource.records.push_back({gitRepository, TagRevision{"v1"}, commit_git, digest_a, git_contract()});
    TestVerifier verifier;
    auto authorities = authority_directory();
    VerifiedRoutedPackageMetadataSource routed(registryDirectory, authorities, locationSource, verifier, gitSource);
    const auto registryRequirement = exact("registry-pkg", registryIdentity, {1, 0, 0, std::nullopt});
    const auto gitRequirement = git_req("git-pkg", gitRepository, TagRevision{"v1"});
    const auto acquired = acquire_resolution_catalog({registryRequirement, gitRequirement}, routed);
    assert(acquired.valid());
    const auto graph = resolve_packages({registryRequirement, gitRequirement}, *acquired.catalog);
    assert(graph.valid() && graph.value->nodes.size() == 2);
    assert(graph.value->roots.size() == 2);
    assert(graph.value->roots[0].identity.index() != graph.value->roots[1].identity.index());
  }
}

int main()
{
  directory_identity_and_validation_tests();
  fallback_empty_and_no_merge_tests();
  unknown_and_failure_tests();
  relocation_and_mirror_order_lock_invariance_tests();
  registry_vs_git_identity_test();
}
