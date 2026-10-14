#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/MetadataStorage.hpp>
#include <vix/cli/project/native/PackageContentStore.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace vix::cli::project::native;

namespace
{
  constexpr const char *digest_x = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  constexpr const char *digest_y = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  constexpr const char *commit = "0123456789abcdef0123456789abcdef01234567";

  // Opaque test-only payload; this does not select a production content form.
  class TestCandidate final : public PackageContentCandidate
  {
  public:
    TestCandidate(std::string expected, std::string bytes, std::string origin)
      : expectedDigest(std::move(expected)), payload(std::move(bytes)), source(std::move(origin)) {}
    std::string expectedDigest;
    std::string payload;
    std::string source;
  };

  class TestVerifier final : public PackageContentVerifier
  {
  public:
    [[nodiscard]] PackageContentVerificationResult verify(
      std::shared_ptr<const PackageContentCandidate> candidate,
      const PackageContentId &expected) const override
    {
      const auto typed = std::dynamic_pointer_cast<const TestCandidate>(candidate);
      if (!typed || typed->expectedDigest != expected.digest())
        return verification_failure(expected, "test verifier rejected candidate/digest binding");
      return verified_content(std::move(candidate), expected, typed->payload);
    }
  };

  Lockfile graph_lock(std::string digestA = digest_x, std::string digestB = digest_x,
                      std::string digestC = digest_y, bool reverseNodes = false,
                      std::string rootAlias = "root")
  {
    Lockfile lock;
    RegistryPackageIdentity aIdentity{"vix", "example", "a"};
    RegistryPackageIdentity bIdentity{"acme", "example", "b"};
    RegistryPackageIdentity cIdentity{"vix", "example", "c"};
    const Version version{1, 0, 0, std::nullopt};
    RegistryLockNode a{registry_node_id(RegistryNodeIdentity{aIdentity, version}), aIdentity, version, std::move(digestA), {}};
    RegistryLockNode b{registry_node_id(RegistryNodeIdentity{bIdentity, version}), bIdentity, version, std::move(digestB), {}};
    RegistryLockNode c{registry_node_id(RegistryNodeIdentity{cIdentity, version}), cIdentity, version, std::move(digestC), {}};
    a.dependencies = {{"bee", b.id}, {"see", c.id}};
    const NormalizedDeclaredRegistry declared{aIdentity, ExactVersion{version}};
    lock.roots.push_back({rootAlias, declared, a.id});
    lock.rootRequirementsDigest = root_requirements_digest({RootRequirementDigestEntry{rootAlias, declared}});
    if (reverseNodes)
    {
      lock.nodes.emplace_back(std::move(c));
      lock.nodes.emplace_back(std::move(b));
      lock.nodes.emplace_back(std::move(a));
    }
    else
    {
      lock.nodes.emplace_back(std::move(a));
      lock.nodes.emplace_back(std::move(b));
      lock.nodes.emplace_back(std::move(c));
    }
    return lock;
  }

  std::shared_ptr<const PackageContentCandidate> candidate(std::string digest,
      std::string payload, std::string origin = "registry")
  { return std::make_shared<TestCandidate>(std::move(digest), std::move(payload), std::move(origin)); }

  VerifiedPackageContent verified(const TestVerifier &verifier, const std::string &digest,
                                  std::string payload, std::string origin = "registry")
  {
    const auto id = PackageContentId::parse(digest);
    assert(id);
    auto result = verifier.verify(candidate(digest, std::move(payload), std::move(origin)), *id);
    assert(result.valid());
    return *result.value;
  }

  std::string serialized(const Lockfile &lock)
  {
    const auto result = serialize_lockfile(lock);
    assert(result.valid());
    return *result.text;
  }

  void content_id_and_type_boundary_test()
  {
    assert(PackageContentId::parse(digest_x));
    assert(!PackageContentId::parse("sha256:ABC"));
    assert(!PackageContentId::parse("sha256:" + std::string(64, 'g')));
    static_assert(!std::is_default_constructible_v<VerifiedPackageContent>);
    static_assert(!std::is_constructible_v<VerifiedPackageContent, PackageContentId,
      std::shared_ptr<const PackageContentCandidate>, std::string>);
    static_assert(!std::is_base_of_v<PackageMetadataCache, InMemoryPackageContentStore>);
    static_assert(!std::is_base_of_v<TrustedRegistryStateStore, InMemoryPackageContentStore>);
  }

  void verification_and_admission_test()
  {
    TestVerifier verifier;
    InMemoryPackageContentStore store;
    const auto x = *PackageContentId::parse(digest_x);
    assert(store.inspect(x).value == PackageContentAvailability::missing);
    const auto failed = verifier.verify(candidate(digest_y, "payload"), x);
    assert(!failed.valid() && failed.issues.size() == 1);
    assert(failed.issues.front().code == PackageContentIssueCode::package_content_verification_failed);
    assert(store.inspect(x).value == PackageContentAvailability::missing);

    const auto accepted = verifier.verify(candidate(digest_x, "payload"), x);
    assert(accepted.valid());
    assert(store.admit(*accepted.value).valid());
    assert(store.inspect(x).value == PackageContentAvailability::available);
    assert(store.size() == 1);
  }

  void deduplication_and_conflict_test()
  {
    TestVerifier verifier;
    InMemoryPackageContentStore store;
    assert(store.admit(verified(verifier, digest_x, "same" )).valid());
    assert(store.admit(verified(verifier, digest_x, "same", "git")).valid());
    assert(store.size() == 1);
    assert(store.admit(verified(verifier, digest_y, "other")).valid());
    assert(store.size() == 2);
    const auto conflict = store.admit(verified(verifier, digest_x, "contradiction"));
    assert(!conflict.valid() && conflict.issues.size() == 1);
    assert(conflict.issues.front().code == PackageContentIssueCode::package_content_conflict);
    assert(store.size() == 2);
  }

  void lock_plan_and_dedup_test()
  {
    const auto lock = graph_lock();
    assert(validate_lockfile(lock).valid());
    assert(validate_lockfile_integrity(lock).valid());
    const auto plan = required_package_content(lock);
    assert(plan.valid() && plan.required.size() == 2);
    assert(plan.required[0].id.digest() == digest_x);
    assert(plan.required[0].nodeIds.size() == 2);
    assert(plan.required[1].id.digest() == digest_y);
    assert(plan.required[1].nodeIds.size() == 1);
    for (const auto &node : lock.nodes)
      assert(std::visit([&](const auto &value)
        { return plan.required[0].id.digest() != value.id; }, node));
  }

  void plan_order_and_lock_immutability_test()
  {
    auto first = graph_lock(digest_x, digest_x, digest_y, false);
    auto reordered = graph_lock(digest_x, digest_x, digest_y, true);
    const auto originalBytes = serialized(first);
    assert(serialized(reordered) == originalBytes);
    const auto firstPlan = required_package_content(first);
    const auto secondPlan = required_package_content(reordered);
    assert(firstPlan.valid() && secondPlan.valid());
    assert(firstPlan.required.size() == secondPlan.required.size());
    for (std::size_t i = 0; i < firstPlan.required.size(); ++i)
    {
      assert(firstPlan.required[i].id == secondPlan.required[i].id);
      assert(firstPlan.required[i].nodeIds == secondPlan.required[i].nodeIds);
    }
    TestVerifier verifier;
    InMemoryPackageContentStore store;
    assert(store.admit(verified(verifier, digest_x, "x" )).valid());
    assert(store.admit(verified(verifier, digest_y, "y" )).valid());
    assert(check_package_content_availability(first, store).complete);
    assert(check_package_content_availability(reordered, store).complete);
    assert(serialized(first) == originalBytes);
  }

  void complete_partial_and_shared_missing_test()
  {
    const auto lock = graph_lock(digest_x, digest_x, digest_y);
    std::vector<std::string> expectedSharedNodes;
    for (const auto &node : lock.nodes)
      std::visit([&](const auto &value)
      {
        if (value.contentDigest == digest_x) expectedSharedNodes.push_back(value.id);
      }, node);
    std::sort(expectedSharedNodes.begin(), expectedSharedNodes.end());
    assert(expectedSharedNodes.size() == 2);

    TestVerifier verifier;
    InMemoryPackageContentStore store;
    assert(store.admit(verified(verifier, digest_y, "y")).valid());
    const auto partial = check_package_content_availability(lock, store);
    assert(partial.valid());
    assert(!partial.complete);
    assert(partial.missing.size() == 1);
    assert(partial.missing.front().id.digest() == digest_x);
    assert(partial.missing.front().nodeIds == expectedSharedNodes);
    assert(partial.issues.size() == 1);
    assert(partial.issues.front().code == PackageContentIssueCode::package_content_unavailable);
    assert(partial.issues.front().digest == digest_x);
    assert(partial.issues.front().nodeIds == expectedSharedNodes);

    InMemoryPackageContentStore empty;
    const auto metadataCompleteContentMissing = check_package_content_availability(lock, empty);
    assert(metadataCompleteContentMissing.valid());
    assert(!metadataCompleteContentMissing.complete);
    const auto missingX = std::find_if(metadataCompleteContentMissing.issues.begin(), metadataCompleteContentMissing.issues.end(),
      [](const auto &problem) { return problem.digest == digest_x; });
    assert(missingX != metadataCompleteContentMissing.issues.end());
    assert(missingX->code == PackageContentIssueCode::package_content_unavailable);
    assert(missingX->nodeIds == expectedSharedNodes);
    assert(metadataCompleteContentMissing.missing.size() == 2);
  }

  void cross_project_reuse_test()
  {
    TestVerifier verifier;
    InMemoryPackageContentStore store;
    assert(store.admit(verified(verifier, digest_x, "shared")).valid());
    const auto projectA = graph_lock(digest_x, digest_x, digest_x, false, "first-root");
    const auto projectB = graph_lock(digest_x, digest_x, digest_x, true, "second-root");
    assert(check_package_content_availability(projectA, store).complete);
    assert(check_package_content_availability(projectB, store).complete);
    assert(store.size() == 1);
  }

  void p10m_independence_test()
  {
    InMemoryTrustedRegistryStateStore trusted;
    const TrustedRegistrySnapshotState state{"vix", 51, digest_x};
    assert(trusted.compare_and_advance(std::nullopt, state).valid());

    InMemoryPackageMetadataCache metadata;
    const GitPackageIdentity identity{"https://example.invalid/shared.git", true};
    const GitRevisionRequirement revision = TagRevision{"v1"};
    PackageContract contract;
    contract.identity = GitPackageContractIdentity{};
    contract.integration = CMakeConfigIntegration{"shared"};
    contract.exports.push_back({"shared", CMakeExportMapping{"Shared::Shared"}});
    const GitResolution resolution{identity.normalizedRepository, revision, commit,
      digest_y, std::move(contract)};
    assert(metadata.write_git(identity, revision, resolution).valid());

    TestVerifier verifier;
    InMemoryPackageContentStore content;
    assert(content.admit(verified(verifier, digest_x, "x")).valid());
    content.clear();
    assert(content.inspect(*PackageContentId::parse(digest_x)).value == PackageContentAvailability::missing);
    const auto stillTrusted = trusted.load("vix");
    assert(stillTrusted.valid() && stillTrusted.state == state);
    const auto stillCached = metadata.read_git(identity, revision);
    assert(stillCached.valid() && stillCached.value->commit == commit);
  }
}

int main()
{
  content_id_and_type_boundary_test();
  verification_and_admission_test();
  deduplication_and_conflict_test();
  lock_plan_and_dedup_test();
  plan_order_and_lock_immutability_test();
  complete_partial_and_shared_missing_test();
  cross_project_reuse_test();
  p10m_independence_test();
  return 0;
}
