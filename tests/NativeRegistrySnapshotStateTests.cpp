#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/Lockfile.hpp>
#include <vix/cli/project/native/MetadataAcquisition.hpp>
#include <vix/cli/project/native/RegistryAuthority.hpp>
#include <vix/cli/project/native/RegistrySnapshotState.hpp>
#include <vix/cli/project/native/Resolver.hpp>
#include <vix/cli/project/native/TomlParser.hpp>

#include <algorithm>
#include <cassert>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace vix::cli::project::native;

static_assert(!std::is_invocable_v<decltype(&check_registry_snapshot_state),
  const std::optional<TrustedRegistrySnapshotState> &, const UnverifiedRegistryObservation &>);

namespace
{
  constexpr const char *digestA = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  constexpr const char *digestB = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  const RegistryPackageIdentity packageA{"vix", "example", "a"};

  struct AcceptVerifier final : RegistryAuthorityVerifier
  {
    bool verify_evidence(const RegistryAuthorityDefinition &, const UnverifiedRegistryObservation &,
      std::string &) const override { return true; }
  };

  struct MemoryGit final : PackageMetadataSource
  {
    RegistryQueryResult registry_releases(const RegistryPackageIdentity &) override
      { return {false, {}, "unexpected Registry delegation"}; }
    GitQueryResult resolve_git(const GitPackageIdentity &, const GitRevisionRequirement &) override
      { return {GitQueryStatus::not_found, std::nullopt, {}}; }
  };

  struct MemoryLocations final : RegistryObservationLocationSource
  {
    std::function<UnverifiedRegistryLocationResult(const RegistryLocation &, const RegistryPackageIdentity &)> reply;
    std::vector<std::string> calls;
    UnverifiedRegistryLocationResult registry_observation_at(const RegistryLocation &location,
      const RegistryPackageIdentity &identity) override
    {
      calls.push_back(location.descriptor + "/" + identity.name);
      return reply(location, identity);
    }
  };

  RegistrySnapshotIdentity snapshot(std::uint64_t sequence, std::string digest = digestA,
    std::string registry = "vix")
  { return {std::move(registry), sequence, std::move(digest)}; }

  UnverifiedRegistryLocationResult response(std::uint64_t sequence, std::string digest = digestA,
    RegistryPackageIdentity query = packageA, std::vector<RegistryCandidate> releases = {})
  {
    UnverifiedRegistryObservation value{snapshot(sequence, std::move(digest), query.registry),
      query, std::move(releases), "test-only verified fixture"};
    return {true, std::move(value), {}};
  }

  VerifiedRegistryObservation verified(AcceptVerifier &verifier, std::uint64_t sequence,
    std::string digest = digestA, std::string registry = "vix")
  {
    auto authorityResult = build_registry_authority_directory({{registry, "authority"}});
    assert(authorityResult.valid());
    RegistryPackageIdentity query{registry, "example", "pkg"};
    UnverifiedRegistryObservation raw{snapshot(sequence, std::move(digest), registry), query, {}, "fixture"};
    auto result = verify_registry_observation(*authorityResult.value, verifier, query, raw);
    assert(result.valid());
    return std::move(*result.observation);
  }

  PackageContract registry_contract(const RegistryPackageIdentity &identity, Version version,
    std::vector<PackageRequirement> requirements = {})
  {
    PackageContract contract;
    contract.identity = RegistryPackageContractIdentity{identity, version};
    contract.packages = std::move(requirements);
    contract.integration = NativeVixIntegration{};
    contract.exports.push_back({"api", NativeExportMapping{"api"}});
    return contract;
  }

  RegistryCandidate candidate(const RegistryPackageIdentity &identity,
    std::vector<PackageRequirement> requirements = {})
  {
    const Version version{1, 0, 0, std::nullopt};
    return {identity, version, digestA, registry_contract(identity, version, std::move(requirements))};
  }

  PackageRequirement requirement(std::string alias, RegistryPackageIdentity identity)
  { return {std::move(alias), RegistryRequirement{std::move(identity), ExactVersion{{1, 0, 0, std::nullopt}}}}; }

  struct Fixture
  {
    RegistryDirectory locations;
    RegistryAuthorityDirectory authorities;
    Fixture()
      : locations(*build_registry_directory({{"vix", {{"first"}, {"second"}}}}).value),
        authorities(*build_registry_authority_directory({{"vix", "authority"}}).value) {}
  };

  void state_transition_tests()
  {
    AcceptVerifier verifier;
    auto first = check_registry_snapshot_state(std::nullopt, verified(verifier, 10, digestA));
    assert(first.valid() && first.proposedState->sequence == 10 && first.proposedState->digest == digestA);

    auto replay = check_registry_snapshot_state(TrustedRegistrySnapshotState{"vix", 10, digestA},
      verified(verifier, 10, digestA));
    assert((replay.valid() && *replay.proposedState == TrustedRegistrySnapshotState{"vix", 10, digestA}));

    auto advanceSameDigest = check_registry_snapshot_state(TrustedRegistrySnapshotState{"vix", 10, digestA},
      verified(verifier, 11, digestA));
    assert(advanceSameDigest.valid() && advanceSameDigest.proposedState->sequence == 11);

    auto advance = check_registry_snapshot_state(TrustedRegistrySnapshotState{"vix", 10, digestA},
      verified(verifier, 11, digestB));
    assert(advance.valid() && advance.proposedState->digest == digestB);

    auto rollback = check_registry_snapshot_state(TrustedRegistrySnapshotState{"vix", 10, digestA},
      verified(verifier, 9, digestB));
    assert(!rollback.valid() && rollback.issues.front().code == RegistrySnapshotStateErrorCode::registry_rollback_detected);

    auto equivocation = check_registry_snapshot_state(TrustedRegistrySnapshotState{"vix", 10, digestA},
      verified(verifier, 10, digestB));
    assert(!equivocation.valid() && equivocation.issues.front().code == RegistrySnapshotStateErrorCode::registry_snapshot_equivocation);

    auto otherRegistry = check_registry_snapshot_state(TrustedRegistrySnapshotState{"acme", 7, digestA},
      verified(verifier, 8, digestB, "vix"));
    assert(!otherRegistry.valid() && otherRegistry.issues.front().code == RegistrySnapshotStateErrorCode::invalid_trusted_snapshot_state);
  }

  void fallback_and_empty_tests()
  {
    Fixture fixture;
    AcceptVerifier verifier;
    MemoryGit git;
    MemoryLocations locations;
    locations.reply = [](const RegistryLocation &location, const RegistryPackageIdentity &)
    {
      if (location.descriptor == "first") return response(40, digestB);
      return response(50, digestA);
    };
    RegistrySnapshotGuard guard(fixture.locations, fixture.authorities, locations, verifier, git,
      {{"vix", 50, digestA}});
    auto accepted = guard.registry_releases(packageA);
    assert(accepted.succeeded && accepted.releases.empty());
    assert(locations.calls.size() == 2);
    assert(guard.pins().size() == 1 && guard.pins()[0].snapshot.sequence == 50);

    locations.calls.clear();
    locations.reply = [](const RegistryLocation &location, const RegistryPackageIdentity &identity)
    {
      if (location.descriptor == "first") return response(50, digestA, identity, {});
      return response(51, digestB, identity, {candidate(identity)});
    };
    RegistrySnapshotGuard emptyGuard(fixture.locations, fixture.authorities, locations, verifier, git,
      {{"vix", 50, digestA}});
    auto empty = emptyGuard.registry_releases(packageA);
    assert(empty.succeeded && empty.releases.empty());
    assert(locations.calls.size() == 1); // acceptable empty observation stops fallback
    assert(emptyGuard.proposed_states().size() == 1 && emptyGuard.proposed_states().front().sequence == 50);
    auto emptyCatalog = acquire_resolution_catalog({requirement("a", packageA)}, emptyGuard);
    assert(emptyCatalog.valid() && emptyCatalog.catalog);
    auto absent = resolve_packages({requirement("a", packageA)}, *emptyCatalog.catalog);
    assert(!absent.valid() && absent.issues.front().code == ResolverErrorCode::no_candidate);

    locations.calls.clear();
    locations.reply = [](const RegistryLocation &location, const RegistryPackageIdentity &identity)
    {
      if (location.descriptor == "first") return response(40, digestB, identity, {}); // stale empty
      return response(50, digestA, identity, {candidate(identity)});
    };
    RegistrySnapshotGuard staleEmpty(fixture.locations, fixture.authorities, locations, verifier, git,
      {{"vix", 50, digestA}});
    auto foundAfterFallback = staleEmpty.registry_releases(packageA);
    assert(foundAfterFallback.succeeded && foundAfterFallback.releases.size() == 1);
    assert(locations.calls.size() == 2);

    locations.calls.clear();
    locations.reply = [](const RegistryLocation &location, const RegistryPackageIdentity &identity)
    {
      if (location.descriptor == "first") return response(50, digestB, identity, {candidate(identity)}); // equivocation
      return response(51, digestA, identity, {candidate(identity)});
    };
    RegistrySnapshotGuard equivocationFallback(fixture.locations, fixture.authorities, locations, verifier, git,
      {{"vix", 50, digestA}});
    auto afterEquivocation = equivocationFallback.registry_releases(packageA);
    assert(afterEquivocation.succeeded && afterEquivocation.releases.size() == 1);
    assert(locations.calls.size() == 2 && equivocationFallback.proposed_states().front().sequence == 51);

    locations.calls.clear();
    locations.reply = [](const RegistryLocation &, const RegistryPackageIdentity &)
      { return UnverifiedRegistryLocationResult{false, std::nullopt, "offline"}; };
    RegistrySnapshotGuard rejected(fixture.locations, fixture.authorities, locations, verifier, git,
      {{"vix", 50, digestA}});
    auto allRejected = rejected.registry_releases(packageA);
    assert(!allRejected.succeeded);
    assert(rejected.last_issues().size() == 3);
    assert(rejected.last_issues().back().code == RegistrySnapshotRoutingErrorCode::all_registry_locations_rejected);
    assert(allRejected.diagnostic.find("no_candidate") == std::string::npos);
  }

  void independent_registry_pin_tests()
  {
    auto locationResult = build_registry_directory({
      {"vix", {{"shared-vix"}}}, {"acme", {{"shared-acme"}}}});
    auto authorityResult = build_registry_authority_directory({
      {"vix", "authority-vix"}, {"acme", "authority-acme"}});
    assert(locationResult.valid() && authorityResult.valid());
    AcceptVerifier verifier;
    MemoryGit git;
    MemoryLocations locations;
    locations.reply = [](const RegistryLocation &, const RegistryPackageIdentity &identity)
    {
      const auto sequence = identity.registry == "vix" ? 50U : 8U;
      const auto digest = identity.registry == "vix" ? digestA : digestB;
      return response(sequence, digest, identity, {});
    };
    RegistrySnapshotGuard guard(*locationResult.value, *authorityResult.value, locations, verifier, git,
      {{"vix", 49, digestA}, {"acme", 7, digestA}});
    assert(guard.registry_releases({"vix", "example", "a"}).succeeded);
    assert(guard.registry_releases({"acme", "example", "b"}).succeeded);
    assert(guard.pins().size() == 2);
    assert(guard.pins()[0].registryId == "acme" && guard.pins()[1].registryId == "vix");
    assert(guard.proposed_states().size() == 2);
  }

  void session_pin_and_acquisition_tests()
  {
    Fixture fixture;
    AcceptVerifier verifier;
    MemoryGit git;
    MemoryLocations locations;
    const RegistryPackageIdentity packageB{"vix", "example", "b"};
    auto aCandidate = candidate(packageA, {requirement("b", packageB)});
    auto bCandidate = candidate(packageB);
    locations.reply = [&](const RegistryLocation &, const RegistryPackageIdentity &identity)
    {
      if (identity.name == "a") return response(50, digestA, identity, {aCandidate});
      return response(51, digestB, identity, {bCandidate});
    };
    RegistrySnapshotGuard mixed(fixture.locations, fixture.authorities, locations, verifier, git);
    auto failed = acquire_resolution_catalog({requirement("a", packageA)}, mixed);
    assert(!failed.valid() && !failed.catalog);
    assert(failed.issues.size() == 1);
    assert(failed.issues.front().code == MetadataAcquisitionErrorCode::registry_source_failure);
    assert(failed.issues.front().message.find("registry_snapshot_changed_during_acquisition") != std::string::npos);
    assert(mixed.pins().size() == 1 && mixed.pins().front().snapshot.sequence == 50);
    assert(mixed.last_issues().size() == 3);
    assert(mixed.last_issues()[0].message.find("registry_snapshot_changed_during_acquisition") != std::string::npos);
    assert(mixed.last_issues()[1].message.find("registry_snapshot_changed_during_acquisition") != std::string::npos);
    assert(mixed.last_issues().back().code == RegistrySnapshotRoutingErrorCode::all_registry_locations_rejected);

    locations.reply = [&](const RegistryLocation &, const RegistryPackageIdentity &identity)
    {
      if (identity.name == "a") return response(50, digestA, identity, {aCandidate});
      return response(50, digestA, identity, {bCandidate});
    };
    RegistrySnapshotGuard coherent(fixture.locations, fixture.authorities, locations, verifier, git);
    auto acquired = acquire_resolution_catalog({requirement("a", packageA)}, coherent);
    assert(acquired.valid() && acquired.catalog);
    auto graph = resolve_packages({requirement("a", packageA)}, *acquired.catalog);
    assert(graph.valid());
    auto lock = make_resolved_lockfile(*graph.value);
    assert(lock.valid() && validate_lockfile_integrity(*lock.value).valid());
    auto text = serialize_lockfile(*lock.value);
    assert(text.valid());
    auto parsed = parse_lockfile_text(*text.text);
    assert(parsed.valid());
    assert(parsed.value->nodes.size() == lock.value->nodes.size());
    const auto idBefore = registry_node_id({packageA, {1, 0, 0, std::nullopt}});
    const auto idAfter = registry_node_id({packageA, {1, 0, 0, std::nullopt}});
    assert(idBefore == idAfter); // snapshot identity is intentionally outside package structural identity
  }

  bool has_session_issue(const RegistrySnapshotGuard &guard, std::string_view text)
  {
    return std::any_of(guard.last_issues().begin(), guard.last_issues().end(), [&](const auto &issue)
    {
      return issue.registryId == "vix" &&
        issue.code == RegistrySnapshotRoutingErrorCode::snapshot_rejected &&
        issue.message.find(text) != std::string::npos;
    });
  }

  void mid_session_older_snapshot_test()
  {
    Fixture fixture;
    AcceptVerifier verifier;
    MemoryGit git;
    MemoryLocations locations;
    const RegistryPackageIdentity packageB{"vix", "example", "b"};
    const auto aCandidate = candidate(packageA, {requirement("b", packageB)});
    const auto bCandidate = candidate(packageB);
    std::vector<RegistrySnapshotIdentity> returned;
    locations.reply = [&](const RegistryLocation &, const RegistryPackageIdentity &identity)
    {
      const auto sequence = identity.name == "a" ? 50U : 49U;
      if (identity.name == "a")
      {
        returned.push_back(snapshot(sequence, digestA));
        return response(50, digestA, identity, {aCandidate});
      }
      returned.push_back(snapshot(sequence, digestB)); // valid digest representing 49/Z
      return response(49, digestB, identity, {bCandidate});
    };

    RegistrySnapshotGuard guard(fixture.locations, fixture.authorities, locations, verifier, git);
    auto result = acquire_resolution_catalog({requirement("a", packageA)}, guard);
    assert(!result.valid() && !result.catalog);
    assert(result.issues.size() == 1 &&
      result.issues.front().code == MetadataAcquisitionErrorCode::registry_source_failure);
    assert(guard.pins().size() == 1);
    assert(guard.pins().front().registryId == "vix");
    assert(guard.pins().front().snapshot == snapshot(50, digestA));
    assert(returned.size() == 3); // both configured locations returned the rejected 49/Z observation
    assert(returned[0] == snapshot(50, digestA));
    assert(returned[1] == snapshot(49, digestB));
    assert(returned[2] == snapshot(49, digestB));
    assert(has_session_issue(guard, "registry_snapshot_changed_during_acquisition"));
    assert(std::all_of(guard.last_issues().begin(), guard.last_issues().end(), [](const auto &issue)
      { return issue.registryId == "vix"; }));
    assert(result.issues.front().message.find("registry_snapshot_changed_during_acquisition") != std::string::npos);
  }

  void mid_session_same_sequence_different_digest_test()
  {
    Fixture fixture;
    AcceptVerifier verifier;
    MemoryGit git;
    MemoryLocations locations;
    const RegistryPackageIdentity packageB{"vix", "example", "b"};
    const auto aCandidate = candidate(packageA, {requirement("b", packageB)});
    const auto bCandidate = candidate(packageB);
    std::vector<RegistrySnapshotIdentity> returned;
    locations.reply = [&](const RegistryLocation &, const RegistryPackageIdentity &identity)
    {
      if (identity.name == "a")
      {
        returned.push_back(snapshot(50, digestA));
        return response(50, digestA, identity, {aCandidate});
      }
      returned.push_back(snapshot(50, digestB));
      return response(50, digestB, identity, {bCandidate});
    };

    RegistrySnapshotGuard guard(fixture.locations, fixture.authorities, locations, verifier, git);
    auto result = acquire_resolution_catalog({requirement("a", packageA)}, guard);
    assert(!result.valid() && !result.catalog);
    assert(result.issues.size() == 1 &&
      result.issues.front().code == MetadataAcquisitionErrorCode::registry_source_failure);
    assert(guard.pins().size() == 1);
    assert(guard.pins().front().registryId == "vix");
    assert(guard.pins().front().snapshot == snapshot(50, digestA));
    assert(returned.size() == 3); // both locations returned 50/B after the session pinned 50/A
    assert(returned[0] == snapshot(50, digestA));
    assert(returned[1] == snapshot(50, digestB));
    assert(returned[2] == snapshot(50, digestB));
    assert(has_session_issue(guard, "registry_snapshot_changed_during_acquisition"));
    assert(result.issues.front().message.find("registry_snapshot_changed_during_acquisition") != std::string::npos);
  }
}

int main()
{
  state_transition_tests();
  fallback_and_empty_tests();
  independent_registry_pin_tests();
  session_pin_and_acquisition_tests();
  mid_session_older_snapshot_test();
  mid_session_same_sequence_different_digest_test();
  return 0;
}
