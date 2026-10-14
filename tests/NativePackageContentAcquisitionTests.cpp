#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/FilesystemPackageContentStore.hpp>
#include <vix/cli/project/native/PackageContentAcquisition.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace vix::cli::project::native;
namespace fs = std::filesystem;

namespace
{
  class TestRoot
  {
  public:
    TestRoot()
    {
      path_ = fs::temp_directory_path() / ("vix-p10qa-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
      std::error_code ec;
      fs::remove_all(path_, ec);
    }
    ~TestRoot() { std::error_code ec; fs::remove_all(path_, ec); }
    [[nodiscard]] const fs::path &path() const noexcept { return path_; }
  private:
    fs::path path_;
  };

  CanonicalPackageEntry file(std::string path, std::string_view bytes)
  {
    CanonicalPackageEntry entry;
    entry.path = std::move(path);
    for (const unsigned char byte : bytes) entry.content.push_back(static_cast<std::byte>(byte));
    return entry;
  }

  CanonicalPackageTree tree(std::string path, std::string_view bytes)
  { return {{file(std::move(path), bytes)}}; }

  CanonicalPackageContentResult encoded(const CanonicalPackageTree &value)
  { return canonicalize_package_content_tree(value); }

  std::string registry_node_id_for(const RegistryPackageIdentity &identity, const Version &version)
  { return registry_node_id(RegistryNodeIdentity{identity, version}); }

  Lockfile lock_from_nodes(std::vector<LockNode> nodes)
  {
    Lockfile lock;
    std::vector<RootRequirementDigestEntry> digestEntries;
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
      const auto nodeId = std::visit([](const auto &node) { return node.id; }, nodes[index]);
      const auto alias = "root-" + nodeId.substr(7);
      std::visit([&](const auto &node)
      {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node, RegistryLockNode>)
        {
          const NormalizedDeclaredRegistry declared{node.identity, ExactVersion{node.version}};
          lock.roots.push_back({alias, declared, node.id});
          digestEntries.push_back({alias, declared});
        }
        else
        {
          const NormalizedDeclaredGit declared{node.normalizedRepository, true, CommitRevision{node.commit}};
          lock.roots.push_back({alias, declared, node.id});
          digestEntries.push_back({alias,
            NormalizedRootGitRequirement{node.normalizedRepository, declared.revision}});
        }
      }, nodes[index]);
      lock.nodes.push_back(std::move(nodes[index]));
    }
    lock.rootRequirementsDigest = root_requirements_digest(digestEntries);
    return lock;
  }

  RegistryLockNode registry_node(std::string registry, std::string name, const PackageContentId &content)
  {
    const RegistryPackageIdentity identity{std::move(registry), "example", std::move(name)};
    const Version version{1, 2, 3, std::nullopt};
    return {registry_node_id_for(identity, version), identity, version, content.digest(), {}};
  }

  GitLockNode git_node(std::string repository, std::string commit, const PackageContentId &content)
  {
    return {git_node_id(GitNodeIdentity{repository, commit}), std::move(repository), true,
      std::move(commit), content.digest(), {}};
  }

  RegistryDirectory registry_directory(std::vector<RegistryDefinition> definitions)
  {
    auto result = build_registry_directory(std::move(definitions));
    assert(result.valid());
    return std::move(*result.value);
  }

  class FakeRegistrySource final : public RegistryPackageContentSource
  {
  public:
    std::vector<std::string> calls;
    std::vector<RegistryPackageContentRequest> requests;
    std::vector<std::pair<std::string, RegistryPackageContentSourceResult>> responses;
    std::vector<std::string> *events{nullptr};

    RegistryPackageContentSourceResult acquire(const RegistryLocation &location,
      const RegistryPackageContentRequest &request) override
    {
      calls.push_back(location.descriptor);
      requests.push_back(request);
      if (events) events->push_back("registry:" + location.descriptor);
      const auto found = std::find_if(responses.begin(), responses.end(), [&](const auto &item)
      { return item.first == location.descriptor; });
      return found == responses.end() ? RegistryPackageContentSourceResult{} : found->second;
    }
  };

  class FakeGitSource final : public GitPackageContentSource
  {
  public:
    std::vector<GitPackageContentRequest> requests;
    GitPackageContentSourceResult response;
    std::vector<std::string> *events{nullptr};

    GitPackageContentSourceResult acquire(const GitPackageContentRequest &request) override
    {
      requests.push_back(request);
      if (events) events->push_back("git:" + request.normalizedRepository);
      return response;
    }
  };

  class CorruptStore final : public PackageContentStore
  {
  public:
    PackageContentStoreResult inspect(const PackageContentId &id) const override
    {
      return {PackageContentAvailability::corrupt, {{PackageContentIssueCode::package_content_store_corrupt,
        id.digest(), {}, "fixture corruption"}}};
    }
    PackageContentAdmissionResult admit(const VerifiedPackageContent &) override
    { assert(false); return {}; }
  };

  PackageContentAcquisitionResult acquire(const Lockfile &lock, const RegistryDirectory &directory,
    FakeRegistrySource &registry, FakeGitSource &git, PackageContentStore &store)
  { return acquire_locked_package_content(lock, directory, registry, git, store); }

  void reuse_and_registry_acquisition_test()
  {
    const auto contentTree = tree("README.md", "registry bytes\n");
    const auto object = encoded(contentTree);
    assert(object.valid());
    const auto lock = lock_from_nodes({registry_node("vix", "sqlite", *object.contentId)});
    const auto lockBefore = serialize_lockfile(lock);
    assert(lockBefore.valid());
    const auto directory = registry_directory({{"vix", {{"mirror-A"}}}});
    InMemoryPackageContentStore store;
    CanonicalPackageContentVerifier verifier;
    const auto token = verifier.verify(std::make_shared<CanonicalPackageContentCandidate>(contentTree), *object.contentId);
    assert(token.valid() && store.admit(*token.value).valid());
    FakeRegistrySource registry;
    FakeGitSource git;
    const auto reused = acquire(lock, directory, registry, git, store);
    assert(reused.valid() && reused.entries.size() == 1);
    assert(reused.entries.front().state == PackageContentAcquisitionState::reused);
    assert(registry.calls.empty() && git.requests.empty());

    InMemoryPackageContentStore empty;
    registry.responses.push_back({"mirror-A", {PackageContentSourceStatus::available,
      *object.canonicalBytes, {}}});
    const auto fetched = acquire(lock, directory, registry, git, empty);
    assert(fetched.valid() && fetched.entries.size() == 1);
    assert(fetched.entries.front().state == PackageContentAcquisitionState::acquired);
    assert(registry.calls == std::vector<std::string>{"mirror-A"});
    assert(registry.requests.front().identity == std::get<RegistryLockNode>(lock.nodes.front()).identity);
    assert(registry.requests.front().version == std::get<RegistryLockNode>(lock.nodes.front()).version);
    assert(registry.requests.front().expectedContent == *object.contentId);
    assert(empty.inspect(*object.contentId).value == PackageContentAvailability::available);
    assert(serialize_lockfile(lock).text == lockBefore.text);
  }

  void git_acquisition_and_exact_request_test()
  {
    const auto contentTree = tree("src/git.cpp", "git tree\n");
    const auto object = encoded(contentTree);
    assert(object.valid());
    const std::string commit(40, 'a');
    const auto lock = lock_from_nodes({git_node("https://example.invalid/repo", commit, *object.contentId)});
    const auto directory = registry_directory({});
    FakeRegistrySource registry;
    FakeGitSource git;
    git.response = {PackageContentSourceStatus::available,
      std::make_shared<CanonicalPackageContentCandidate>(contentTree), {}};
    InMemoryPackageContentStore store;
    const auto result = acquire(lock, directory, registry, git, store);
    assert(result.valid() && result.entries.front().state == PackageContentAcquisitionState::acquired);
    assert(registry.calls.empty() && git.requests.size() == 1);
    assert(git.requests.front().normalizedRepository == "https://example.invalid/repo");
    assert(git.requests.front().fullCommit == commit);
    assert(git.requests.front().expectedContent == *object.contentId);
    assert(store.inspect(*object.contentId).value == PackageContentAvailability::available);
  }

  void digest_mismatch_and_lock_immutability_test()
  {
    const auto expectedTree = tree("expected", "locked");
    const auto wrongTree = tree("wrong", "payload");
    const auto expected = encoded(expectedTree);
    const auto wrong = encoded(wrongTree);
    assert(expected.valid() && wrong.valid() && expected.contentId != wrong.contentId);
    const auto lock = lock_from_nodes({registry_node("vix", "mismatch", *expected.contentId)});
    const auto before = serialize_lockfile(lock);
    assert(before.valid());
    const auto directory = registry_directory({{"vix", {{"A"}, {"B"}}}});
    FakeRegistrySource registry;
    registry.responses.push_back({"A", {PackageContentSourceStatus::available, *wrong.canonicalBytes, {}}});
    registry.responses.push_back({"B", {PackageContentSourceStatus::available, *expected.canonicalBytes, {}}});
    FakeGitSource git;
    InMemoryPackageContentStore store;
    const auto result = acquire(lock, directory, registry, git, store);
    assert(!result.valid() && result.issues.front().code == PackageContentAcquisitionIssueCode::package_content_digest_mismatch);
    assert(registry.calls == std::vector<std::string>{"A"});
    assert(store.inspect(*expected.contentId).value == PackageContentAvailability::missing);
    assert(serialize_lockfile(lock).text == before.text);

    const std::string commit(40, 'b');
    const auto gitLock = lock_from_nodes({git_node("https://example.invalid/wrong", commit, *expected.contentId)});
    git.response = {PackageContentSourceStatus::available,
      std::make_shared<CanonicalPackageContentCandidate>(wrongTree), {}};
    const auto gitBefore = serialize_lockfile(gitLock);
    const auto gitResult = acquire(gitLock, directory, registry, git, store);
    assert(!gitResult.valid() && gitResult.issues.front().code == PackageContentAcquisitionIssueCode::package_content_digest_mismatch);
    assert(store.inspect(*expected.contentId).value == PackageContentAvailability::missing);
    assert(serialize_lockfile(gitLock).text == gitBefore.text);
  }

  void registry_integrity_and_mirror_routing_test()
  {
    const auto contentTree = tree("pkg", "expected\n");
    const auto object = encoded(contentTree);
    assert(object.valid());
    const auto lock = lock_from_nodes({registry_node("vix", "routing", *object.contentId)});

    {
      const auto directory = registry_directory({{"vix", {{"A"}, {"B"}, {"C"}}}});
      FakeRegistrySource registry;
      registry.responses.push_back({"A", {PackageContentSourceStatus::unavailable, std::nullopt, "offline"}});
      registry.responses.push_back({"B", {PackageContentSourceStatus::available, *object.canonicalBytes, {}}});
      registry.responses.push_back({"C", {PackageContentSourceStatus::available, *object.canonicalBytes, {}}});
      FakeGitSource git;
      InMemoryPackageContentStore store;
      const auto result = acquire(lock, directory, registry, git, store);
      assert(result.valid() && registry.calls == std::vector<std::string>({"A", "B"}));
    }
    {
      const auto directory = registry_directory({{"vix", {{"A"}, {"B"}}}});
      FakeRegistrySource registry;
      registry.responses.push_back({"A", {PackageContentSourceStatus::available,
        std::string{"not canonical"}, {}}});
      registry.responses.push_back({"B", {PackageContentSourceStatus::available, *object.canonicalBytes, {}}});
      FakeGitSource git;
      InMemoryPackageContentStore store;
      const auto result = acquire(lock, directory, registry, git, store);
      assert(!result.valid());
      assert(result.issues.front().code == PackageContentAcquisitionIssueCode::package_content_integrity_failure);
      assert(registry.calls == std::vector<std::string>{"A"});
      assert(store.inspect(*object.contentId).value == PackageContentAvailability::missing);
    }
    {
      const auto directory = registry_directory({{"vix", {{"A"}, {"B"}}}});
      FakeRegistrySource registry;
      registry.responses.push_back({"A", {PackageContentSourceStatus::available, *object.canonicalBytes, {}}});
      registry.responses.push_back({"B", {PackageContentSourceStatus::available, *object.canonicalBytes, {}}});
      FakeGitSource git;
      InMemoryPackageContentStore store;
      const auto result = acquire(lock, directory, registry, git, store);
      assert(result.valid() && registry.calls == std::vector<std::string>{"A"});
    }
  }

  void local_corruption_and_all_transport_failure_test()
  {
    const auto contentTree = tree("pkg", "payload");
    const auto object = encoded(contentTree);
    assert(object.valid());
    const auto lock = lock_from_nodes({registry_node("vix", "unavailable", *object.contentId)});
    const auto directory = registry_directory({{"vix", {{"A"}, {"B"}}}});
    FakeRegistrySource registry;
    FakeGitSource git;
    CorruptStore corrupt;
    const auto corruptResult = acquire(lock, directory, registry, git, corrupt);
    assert(!corruptResult.valid() && registry.calls.empty() && git.requests.empty());
    assert(corruptResult.issues.front().code == PackageContentAcquisitionIssueCode::package_content_store_failure);

    InMemoryPackageContentStore empty;
    registry.responses.push_back({"A", {PackageContentSourceStatus::unavailable, std::nullopt, "absent"}});
    registry.responses.push_back({"B", {PackageContentSourceStatus::transport_failure, std::nullopt, "temporary"}});
    const auto unavailable = acquire(lock, directory, registry, git, empty);
    assert(!unavailable.valid());
    assert(unavailable.issues.front().code == PackageContentAcquisitionIssueCode::package_content_transport_failure);
    assert(registry.calls == std::vector<std::string>({"A", "B"}));
    assert(empty.inspect(*object.contentId).value == PackageContentAvailability::missing);

    const auto unknownLock = lock_from_nodes({registry_node("missing", "unknown", *object.contentId)});
    const auto unknown = registry_directory({});
    FakeRegistrySource unknownRegistry;
    FakeGitSource unknownGit;
    const auto unknownResult = acquire(unknownLock, unknown, unknownRegistry, unknownGit, empty);
    assert(!unknownResult.valid());
    assert(unknownResult.issues.front().code == PackageContentAcquisitionIssueCode::unknown_registry);
    assert(unknownRegistry.calls.empty());
  }

  void mixed_sources_dedup_and_order_test()
  {
    const auto treeA = tree("a", "registry A");
    const auto treeB = tree("b", "git B");
    const auto treeC = tree("c", "registry C");
    const auto objectA = encoded(treeA);
    const auto objectB = encoded(treeB);
    const auto objectC = encoded(treeC);
    assert(objectA.valid() && objectB.valid() && objectC.valid());
    const auto regA = registry_node("vix", "a", *objectA.contentId);
    const auto gitB = git_node("https://example.invalid/b", std::string(40, 'c'), *objectB.contentId);
    const auto regC = registry_node("acme", "c", *objectC.contentId);
    auto lockA = lock_from_nodes({regA, gitB, regC});
    auto lockB = lock_from_nodes({regC, regA, gitB});
    const auto dir = registry_directory({{"acme", {{"C"}}}, {"vix", {{"A"}}}});

    const auto run = [&](const Lockfile &lock, std::vector<std::string> &events)
    {
      FakeRegistrySource registry;
      registry.events = &events;
      registry.responses = {{"A", {PackageContentSourceStatus::available, *objectA.canonicalBytes, {}}},
        {"C", {PackageContentSourceStatus::available, *objectC.canonicalBytes, {}}}};
      FakeGitSource git;
      git.events = &events;
      git.response = {PackageContentSourceStatus::available,
        std::make_shared<CanonicalPackageContentCandidate>(treeB), {}};
      InMemoryPackageContentStore store;
      const auto acquired = acquire(lock, dir, registry, git, store);
      assert(acquired.valid() && acquired.entries.size() == 3);
      assert(store.inspect(*objectA.contentId).value == PackageContentAvailability::available);
      assert(store.inspect(*objectB.contentId).value == PackageContentAvailability::available);
      assert(store.inspect(*objectC.contentId).value == PackageContentAvailability::available);
    };
    std::vector<std::string> orderA;
    std::vector<std::string> orderB;
    run(lockA, orderA);
    run(lockB, orderB);
    assert(orderA == orderB);

    // Duplicate digest across two same-source nodes is acquired once.
    auto sharedLock = lock_from_nodes({registry_node("vix", "first", *objectA.contentId),
      registry_node("acme", "second", *objectA.contentId)});
    FakeRegistrySource sharedRegistry;
    sharedRegistry.responses = {{"A", {PackageContentSourceStatus::available, *objectA.canonicalBytes, {}}}};
    FakeGitSource sharedGit;
    InMemoryPackageContentStore sharedStore;
    const auto sharedDir = registry_directory({{"vix", {{"A"}}}, {"acme", {{"A"}}}});
    const auto sharedResult = acquire(sharedLock, sharedDir, sharedRegistry, sharedGit, sharedStore);
    assert(sharedResult.valid() && sharedResult.entries.size() == 1);
    assert(sharedRegistry.calls.size() == 1 && sharedGit.requests.empty());
    assert(sharedResult.entries.front().nodeIds.size() == 2);

    // If Registry and Git nodes share a digest, sorted node ID selects exactly
    // one source; the other source is never used as implicit fallback.
    const auto crossLock = lock_from_nodes({registry_node("vix", "cross", *objectA.contentId),
      git_node("https://example.invalid/cross", std::string(40, 'd'), *objectA.contentId)});
    const auto crossPlan = required_package_content(crossLock);
    assert(crossPlan.valid() && crossPlan.required.size() == 1);
    const auto selected = std::find_if(crossLock.nodes.begin(), crossLock.nodes.end(), [&](const auto &node)
      { return std::visit([&](const auto &value) { return value.id == crossPlan.required.front().nodeIds.front(); }, node); });
    assert(selected != crossLock.nodes.end());
    FakeRegistrySource crossRegistry;
    crossRegistry.responses = {{"A", {PackageContentSourceStatus::available, *objectA.canonicalBytes, {}}}};
    FakeGitSource crossGit;
    crossGit.response = {PackageContentSourceStatus::available,
      std::make_shared<CanonicalPackageContentCandidate>(treeA), {}};
    InMemoryPackageContentStore crossStore;
    const auto crossDir = registry_directory({{"vix", {{"A"}}}});
    const auto crossResult = acquire(crossLock, crossDir, crossRegistry, crossGit, crossStore);
    assert(crossResult.valid() && crossResult.entries.size() == 1);
    const bool selectedRegistry = std::holds_alternative<RegistryLockNode>(*selected);
    assert((crossRegistry.calls.size() == 1) == selectedRegistry);
    assert((crossGit.requests.size() == 1) == !selectedRegistry);

    FakeRegistrySource failedRegistry;
    failedRegistry.responses = {{"A", {PackageContentSourceStatus::transport_failure, std::nullopt, "failed"}}};
    FakeGitSource failedGit;
    failedGit.response = {PackageContentSourceStatus::transport_failure, nullptr, "failed"};
    InMemoryPackageContentStore failedStore;
    const auto failedCross = acquire(crossLock, crossDir, failedRegistry, failedGit, failedStore);
    assert(!failedCross.valid());
    assert((failedRegistry.calls.size() == 1) == selectedRegistry);
    assert((failedGit.requests.size() == 1) == !selectedRegistry);
    assert(failedStore.inspect(*objectA.contentId).value == PackageContentAvailability::missing);
  }

  void restart_reuse_and_provider_boundary_test()
  {
    const auto contentTree = tree("persisted", "payload");
    const auto object = encoded(contentTree);
    assert(object.valid());
    const auto lock = lock_from_nodes({registry_node("vix", "restart", *object.contentId)});
    const auto directory = registry_directory({{"vix", {{"A"}}}});
    TestRoot root;
    {
      FilesystemPackageContentStore first{root.path()};
      FakeRegistrySource registry;
      registry.responses = {{"A", {PackageContentSourceStatus::available, *object.canonicalBytes, {}}}};
      FakeGitSource git;
      const auto result = acquire(lock, directory, registry, git, first);
      assert(result.valid() && result.entries.front().state == PackageContentAcquisitionState::acquired);
      assert(registry.calls.size() == 1);
    }
    FilesystemPackageContentStore reopened{root.path()};
    FakeRegistrySource registry;
    FakeGitSource git;
    const auto reused = acquire(lock, directory, registry, git, reopened);
    assert(reused.valid() && reused.entries.front().state == PackageContentAcquisitionState::reused);
    assert(registry.calls.empty() && git.requests.empty());

    static_assert(!std::is_constructible_v<VerifiedPackageContent, PackageContentId,
      std::shared_ptr<const PackageContentCandidate>, std::string>);
    static_assert(std::is_same_v<decltype(GitPackageContentSourceResult::candidate),
      std::shared_ptr<const CanonicalPackageContentCandidate>>);
  }
}

int main()
{
  reuse_and_registry_acquisition_test();
  git_acquisition_and_exact_request_test();
  digest_mismatch_and_lock_immutability_test();
  registry_integrity_and_mirror_routing_test();
  local_corruption_and_all_transport_failure_test();
  mixed_sources_dedup_and_order_test();
  restart_reuse_and_provider_boundary_test();
  return 0;
}
