#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/Lockfile.hpp>
#include <vix/cli/project/native/MetadataAcquisition.hpp>
#include <vix/cli/project/native/RegistryAuthority.hpp>
#include <vix/cli/project/native/Resolver.hpp>

#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace vix::cli::project::native;

namespace
{
  constexpr const char *digest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  const RegistryPackageIdentity identity{"vix", "softadastra", "sqlite"};

  RegistryCandidate candidate(Version version = {1, 2, 0, std::nullopt}, std::string contentDigest = digest)
  {
    PackageContract contract;
    contract.identity = RegistryPackageContractIdentity{identity, version};
    contract.integration = NativeVixIntegration{};
    contract.exports.push_back({"core", NativeExportMapping{"core"}});
    return {identity, std::move(version), std::move(contentDigest), std::move(contract)};
  }

  std::string evidence_for(const UnverifiedRegistryObservation &value, std::string_view authority);

  UnverifiedRegistryObservation observation(std::uint64_t sequence = 7, std::string snapshotDigest = digest,
      RegistryPackageIdentity query = identity, std::vector<RegistryCandidate> releases = {candidate()})
  {
    UnverifiedRegistryObservation value{{"vix", sequence, std::move(snapshotDigest)},
      std::move(query), std::move(releases), {}};
    value.authorityEvidence = evidence_for(value, "vix-authority");
    return value;
  }

  std::string evidence_for(const UnverifiedRegistryObservation &value, std::string_view authority)
  {
    std::string result = std::string(authority) + "|" + value.snapshot.registryId + "|" +
      std::to_string(value.snapshot.sequence) + "|" + value.snapshot.digest + "|" + value.query.registry +
      "|" + value.query.publisher + "|" + value.query.name;
    for (const auto &release : value.releases)
    {
      result += "|" + release.identity.registry + "|" + release.identity.publisher + "|" + release.identity.name +
        "|" + std::to_string(release.version.major) + "." + std::to_string(release.version.minor) + "." +
        std::to_string(release.version.patch) + "|" + release.contentDigest;
      for (const auto &requirement : release.contract.packages)
      {
        result += "|dep:" + requirement.alias + ":" + std::to_string(requirement.source.index());
        if (const auto *registry = std::get_if<RegistryRequirement>(&requirement.source))
          result += ":" + registry->identity.registry + ":" + registry->identity.publisher + ":" + registry->identity.name;
        else
          result += ":" + std::get<GitRequirement>(requirement.source).normalizedRepository;
      }
    }
    return result;
  }

  struct MemoryVerifier final : RegistryAuthorityVerifier
  {
    bool verify_evidence(const RegistryAuthorityDefinition &authority,
      const UnverifiedRegistryObservation &value, std::string &diagnostic) const override
    {
      if (value.authorityEvidence == evidence_for(value, authority.authorityId)) return true;
      diagnostic = "deterministic evidence does not bind this Registry snapshot and query";
      return false;
    }
  };

  struct MemoryLocation final : RegistryObservationLocationSource
  {
    struct Reply { std::string location; UnverifiedRegistryLocationResult result; };
    std::vector<Reply> replies;
    std::vector<std::string> calls;
    UnverifiedRegistryLocationResult registry_observation_at(
      const RegistryLocation &location, const RegistryPackageIdentity &) override
    {
      calls.push_back(location.descriptor);
      for (const auto &reply : replies) if (reply.location == location.descriptor) return reply.result;
      return {false, std::nullopt, "location unavailable"};
    }
  };

  struct EmptyGit final : PackageMetadataSource
  {
    RegistryQueryResult registry_releases(const RegistryPackageIdentity &) override { return {false, {}, "unexpected"}; }
    GitQueryResult resolve_git(const GitPackageIdentity &, const GitRevisionRequirement &) override
      { return {GitQueryStatus::not_found, std::nullopt, {}}; }
  };

  RegistryDirectory make_locations(std::vector<std::string> descriptors)
  {
    RegistryDefinition definition{"vix", {}};
    for (auto &descriptor : descriptors) definition.locations.push_back({std::move(descriptor)});
    auto result = build_registry_directory({std::move(definition)});
    assert(result.valid());
    return std::move(*result.value);
  }

  RegistryAuthorityDirectory make_authorities(std::string authority = "vix-authority")
  {
    auto result = build_registry_authority_directory({{"vix", std::move(authority)}});
    assert(result.valid());
    return std::move(*result.value);
  }

  void directory_and_verified_type_tests()
  {
    auto duplicate = build_registry_authority_directory({{"vix", "A"}, {"vix", "B"}});
    assert(!duplicate.valid() && duplicate.issues.front().code == RegistryAuthorityDirectoryErrorCode::duplicate_registry_authority);
    auto sharedAuthority = build_registry_authority_directory({{"acme", "shared"}, {"vix", "shared"}});
    assert(sharedAuthority.valid());
    auto authorities = make_authorities();
    MemoryVerifier verifier;
    auto valid = verify_registry_observation(authorities, verifier, identity, observation());
    assert(valid.valid() && valid.observation->snapshot().sequence == 7);
    auto noAuthority = build_registry_authority_directory({});
    assert(noAuthority.valid());
    MemoryLocation unreachable;
    auto noTrust = route_verified_registry_observation(make_locations({"plausible-mirror"}),
      *noAuthority.value, unreachable, verifier, identity);
    assert(!noTrust.valid() && noTrust.issues.front().code == VerifiedRegistryRoutingErrorCode::unknown_registry_authority);
    assert(unreachable.calls.empty());

    const RegistryPackageIdentity other{"acme", "softadastra", "sqlite"};
    auto wrongRegistry = verify_registry_observation(authorities, verifier, other,
      UnverifiedRegistryObservation{{"acme", 1, digest}, other, {}, "proof"});
    assert(!wrongRegistry.valid() && wrongRegistry.issues.front().code == RegistryVerificationErrorCode::unknown_registry_authority);
    auto badSnapshot = observation(0);
    auto invalid = verify_registry_observation(authorities, verifier, identity, badSnapshot);
    assert(!invalid.valid() && invalid.issues.front().code == RegistryVerificationErrorCode::invalid_snapshot_identity);
  }

  void binding_and_tampering_tests()
  {
    auto authorities = make_authorities();
    MemoryVerifier verifier;
    auto wrongRegistry = observation();
    wrongRegistry.snapshot.registryId = "acme";
    assert(verify_registry_observation(authorities, verifier, identity, wrongRegistry).issues.front().code ==
      RegistryVerificationErrorCode::registry_binding_mismatch);

    auto wrongQuery = observation();
    wrongQuery.query.name = "http";
    assert(verify_registry_observation(authorities, verifier, identity, wrongQuery).issues.front().code ==
      RegistryVerificationErrorCode::package_query_binding_mismatch);

    auto alteredRelease = observation();
    alteredRelease.releases[0].version.patch++;
    assert(verify_registry_observation(authorities, verifier, identity, alteredRelease).issues.front().code ==
      RegistryVerificationErrorCode::authority_verification_failed);
    auto alteredContent = observation();
    alteredContent.releases[0].contentDigest = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    assert(verify_registry_observation(authorities, verifier, identity, alteredContent).issues.front().code ==
      RegistryVerificationErrorCode::authority_verification_failed);
    auto alteredDependency = observation();
    alteredDependency.releases[0].contract.packages.push_back(
      PackageRequirement{"codec", RegistryRequirement{{"vix", "example", "codec"}, ExactVersion{{2, 0, 0, std::nullopt}}}});
    alteredDependency.authorityEvidence = evidence_for(alteredDependency, "vix-authority");
    alteredDependency.releases[0].contract.packages[0].alias = "tampered";
    assert(verify_registry_observation(authorities, verifier, identity, alteredDependency).issues.front().code ==
      RegistryVerificationErrorCode::authority_verification_failed);
    auto alteredSequence = observation();
    alteredSequence.snapshot.sequence++;
    assert(verify_registry_observation(authorities, verifier, identity, alteredSequence).issues.front().code ==
      RegistryVerificationErrorCode::authority_verification_failed);
    auto alteredSnapshotDigest = observation();
    alteredSnapshotDigest.snapshot.digest = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    assert(verify_registry_observation(authorities, verifier, identity, alteredSnapshotDigest).issues.front().code ==
      RegistryVerificationErrorCode::authority_verification_failed);
  }

  void routing_pipeline_tests()
  {
    auto locations = make_locations({"mirror-a", "mirror-b"});
    auto authorities = make_authorities();
    MemoryVerifier verifier;
    MemoryLocation source;
    auto bad = observation();
    bad.releases[0].version.patch++;
    source.replies = {{"mirror-a", {true, bad, {}}}, {"mirror-b", {true, observation(), {}}}};
    auto routed = route_verified_registry_observation(locations, authorities, source, verifier, identity);
    assert(routed.valid() && source.calls.size() == 2 && source.calls[1] == "mirror-b");

    MemoryLocation emptySource;
    emptySource.replies = {{"mirror-a", {true, observation(8, digest, identity, {}), {}}},
      {"mirror-b", {true, observation(), {}}}};
    auto emptyRouted = route_verified_registry_observation(locations, authorities, emptySource, verifier, identity);
    assert(emptyRouted.valid() && emptyRouted.observation->releases().empty() && emptySource.calls.size() == 1);
    EmptyGit git;
    VerifiedRoutedPackageMetadataSource packageSource(locations, authorities, emptySource, verifier, git);
    PackageRequirement root{"sqlite", RegistryRequirement{identity, ExactVersion{{1, 2, 0, std::nullopt}}}};
    auto acquired = acquire_resolution_catalog({root}, packageSource);
    assert(acquired.valid());
    auto unresolved = resolve_packages({root}, *acquired.catalog);
    assert(!unresolved.valid() && unresolved.issues.front().code == ResolverErrorCode::no_candidate);

    MemoryLocation failed;
    failed.replies = {{"mirror-a", {true, bad, {}}}, {"mirror-b", {false, std::nullopt, "unavailable"}}};
    auto allBad = route_verified_registry_observation(locations, authorities, failed, verifier, identity);
    assert(!allBad.valid() && allBad.issues.size() == 3);
    assert(allBad.issues.back().code == VerifiedRegistryRoutingErrorCode::all_registry_locations_untrusted_or_failed);
    VerifiedRoutedPackageMetadataSource failedSource(locations, authorities, failed, verifier, git);
    auto acquisitionFailure = acquire_resolution_catalog({root}, failedSource);
    assert(!acquisitionFailure.valid() && acquisitionFailure.issues.front().code == MetadataAcquisitionErrorCode::registry_source_failure);

    assert(verify_registry_observation(authorities, verifier, identity, observation(10)).valid());
    assert(verify_registry_observation(authorities, verifier, identity, observation(11)).valid());
    RegistrySnapshotIdentity forkA{"vix", 42, digest};
    RegistrySnapshotIdentity forkB{"vix", 42, "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
    assert(forkA != forkB);
  }

  void authority_does_not_enter_lock_identity_test()
  {
    auto locations = make_locations({"host-a"});
    MemoryLocation sourceA;
    sourceA.replies = {{"host-a", {true, observation(), {}}}};
    MemoryVerifier verifier;
    auto authoritiesA = make_authorities("vix-authority");
    auto authoritiesB = make_authorities("relocated-authority-handle");
    auto proofB = observation();
    proofB.authorityEvidence = evidence_for(proofB, "relocated-authority-handle");
    MemoryLocation sourceB;
    sourceB.replies = {{"host-a", {true, proofB, {}}}};
    EmptyGit git;
    PackageRequirement root{"sqlite", RegistryRequirement{identity, ExactVersion{{1, 2, 0, std::nullopt}}}};
    VerifiedRoutedPackageMetadataSource routedA(locations, authoritiesA, sourceA, verifier, git);
    auto catalogA = acquire_resolution_catalog({root}, routedA);
    assert(catalogA.valid());
    auto graphA = resolve_packages({root}, *catalogA.catalog);
    assert(graphA.valid());
    auto lockA = make_resolved_lockfile(*graphA.value);
    assert(lockA.valid() && validate_lockfile_integrity(*lockA.value).valid());

    VerifiedRoutedPackageMetadataSource routedB(locations, authoritiesB, sourceB, verifier, git);
    auto catalogB = acquire_resolution_catalog({root}, routedB);
    assert(catalogB.valid());
    auto graphB = resolve_packages({root}, *catalogB.catalog);
    assert(graphB.valid());
    auto lockB = make_resolved_lockfile(*graphB.value);
    assert(lockB.valid() && validate_lockfile_integrity(*lockB.value).valid());
    assert(std::get<RegistryLockNode>(lockA.value->nodes.front()).id == std::get<RegistryLockNode>(lockB.value->nodes.front()).id);
    assert(serialize_lockfile(*lockA.value).text == serialize_lockfile(*lockB.value).text);
  }
}

int main()
{
  directory_and_verified_type_tests();
  binding_and_tampering_tests();
  routing_pipeline_tests();
  authority_does_not_enter_lock_identity_test();
}
