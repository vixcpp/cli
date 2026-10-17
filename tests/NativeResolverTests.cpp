#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/Resolver.hpp>
#include <vix/cli/project/native/TomlParser.hpp>
#include <vix/cli/project/native/Validation.hpp>

#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace vix::cli::project::native;

namespace
{
  constexpr const char *digest_a = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  constexpr const char *digest_b = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  constexpr const char *commit_a = "0123456789abcdef0123456789abcdef01234567";
  constexpr const char *commit_b = "1123456789abcdef0123456789abcdef01234567";

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

  PackageRequirement registry_req(std::string alias, std::string name, VersionRequirement requirement)
  {
    return {std::move(alias), RegistryRequirement{{"vix", "example", std::move(name)}, std::move(requirement)}};
  }

  PackageRequirement exact(std::string alias, std::string name, Version version)
  {
    return registry_req(std::move(alias), std::move(name), ExactVersion{std::move(version)});
  }

  PackageRequirement compatible(std::string alias, std::string name, Version version)
  {
    return registry_req(std::move(alias), std::move(name), CompatibleVersion{std::move(version)});
  }

  PackageRequirement git_req(std::string alias, std::string repository, GitRevisionRequirement revision)
  {
    return {std::move(alias), GitRequirement{std::move(repository), true, std::move(revision)}};
  }

  PackageContract registry_contract(const RegistryPackageIdentity &identity, Version version,
                                    std::vector<PackageRequirement> dependencies = {})
  {
    PackageContract contract;
    contract.identity = RegistryPackageContractIdentity{identity, std::move(version)};
    contract.packages = std::move(dependencies);
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

  RegistryCandidate candidate(std::string name, Version version, std::vector<PackageRequirement> dependencies = {},
                              std::string contentDigest = digest_a)
  {
    RegistryPackageIdentity identity{"vix", "example", std::move(name)};
    auto contract = registry_contract(identity, version, std::move(dependencies));
    return {std::move(identity), std::move(version), std::move(contentDigest), std::move(contract)};
  }

  struct MemoryProvider final : ResolutionMetadataProvider
  {
    std::vector<RegistryCandidate> registry;
    std::vector<GitResolution> git;

    std::vector<RegistryCandidate> registry_candidates(const RegistryPackageIdentity &identity) const override
    {
      std::vector<RegistryCandidate> result;
      for (const auto &entry : registry) if (entry.identity == identity) result.push_back(entry);
      return result;
    }

    std::vector<GitResolution> git_resolutions(const std::string &repository,
                                               const GitRevisionRequirement &revision) const override
    {
      std::vector<GitResolution> result;
      for (const auto &entry : git)
        if (entry.normalizedRepository == repository && same_revision(entry.requestedRevision, revision)) result.push_back(entry);
      return result;
    }
  };

  const ResolvedRegistryNode &registry_node(const ResolvedGraph &graph, std::string_view name)
  {
    const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(), [&](const auto &node)
    {
      const auto *registry = std::get_if<ResolvedRegistryNode>(&node);
      return registry && std::string_view{registry->identity.name} == name;
    });
    assert(found != graph.nodes.end());
    return std::get<ResolvedRegistryNode>(*found);
  }

  void basic_registry_tests()
  {
    MemoryProvider provider;
    provider.registry = {
      candidate("fmt", {1, 2, 3, std::nullopt}),
      candidate("fmt", {1, 3, 0, std::nullopt}),
      candidate("fmt", {1, 3, 0, "beta.1"})};

    auto result = resolve_packages({exact("fmt", "fmt", {1, 2, 3, std::nullopt})}, provider);
    assert(result.valid() && registry_node(*result.value, "fmt").version == (Version{1, 2, 3, std::nullopt}));

    result = resolve_packages({compatible("fmt", "fmt", {1, 2, 0, std::nullopt})}, provider);
    assert(result.valid() && registry_node(*result.value, "fmt").version == (Version{1, 3, 0, std::nullopt}));

    provider.registry.push_back(candidate("fmt", {1, 2, 3, "beta.1"}));
    result = resolve_packages({exact("fmt", "fmt", {1, 2, 3, "beta.1"})}, provider);
    assert(result.valid() && registry_node(*result.value, "fmt").version == (Version{1, 2, 3, "beta.1"}));

    result = resolve_packages({compatible("fmt", "fmt", {1, 2, 0, std::nullopt})}, provider);
    assert(result.valid() && !registry_node(*result.value, "fmt").version.prerelease);

    provider.registry = {
      candidate("core", {2, 3, 0, std::nullopt}), candidate("core", {2, 4, 0, std::nullopt}),
      candidate("core", {2, 8, 0, std::nullopt}), candidate("core", {3, 0, 0, std::nullopt})};
    result = resolve_packages({compatible("a", "core", {2, 1, 0, std::nullopt}),
                               compatible("b", "core", {2, 4, 0, std::nullopt})}, provider);
    assert(result.valid() && registry_node(*result.value, "core").version == (Version{2, 8, 0, std::nullopt}));
    assert(result.value->roots.size() == 2 && result.value->nodes.size() == 1);
    assert(result.value->roots[0].identity == result.value->roots[1].identity);

    provider.registry = {candidate("core", {2, 4, 1, std::nullopt})};
    result = resolve_packages({compatible("root", "core", {2, 0, 0, std::nullopt}),
                               exact("transitive", "core", {2, 4, 1, std::nullopt})}, provider);
    assert(result.valid());
    result = resolve_packages({compatible("root", "core", {2, 0, 0, std::nullopt}),
                               exact("transitive", "core", {3, 0, 0, std::nullopt})}, provider);
    assert(!result.valid() && result.issues.front().code == ResolverErrorCode::constraint_conflict);
  }

  void transitive_alias_and_conflict_tests()
  {
    MemoryProvider provider;
    provider.registry.push_back(candidate("a", {1, 0, 0, std::nullopt},
      {compatible("codec", "shared", {1, 2, 0, std::nullopt})}));
    provider.registry.push_back(candidate("b", {1, 0, 0, std::nullopt},
      {compatible("wire", "shared", {1, 4, 0, std::nullopt})}));
    provider.registry.push_back(candidate("shared", {1, 4, 0, std::nullopt}));
    provider.registry.push_back(candidate("shared", {1, 7, 0, std::nullopt}));
    auto result = resolve_packages({exact("root-a", "a", {1, 0, 0, std::nullopt}),
                                    exact("root-b", "b", {1, 0, 0, std::nullopt})}, provider);
    assert(result.valid());
    assert(registry_node(*result.value, "shared").version == (Version{1, 7, 0, std::nullopt}));
    const auto &a = registry_node(*result.value, "a");
    const auto &b = registry_node(*result.value, "b");
    assert(a.dependencies.size() == 1 && a.dependencies.front().alias == "codec");
    assert(b.dependencies.size() == 1 && b.dependencies.front().alias == "wire");
    assert(a.dependencies.front().identity == b.dependencies.front().identity);

    provider.registry.back().version = {2, 0, 0, std::nullopt};
    provider.registry.back().contract = registry_contract(provider.registry.back().identity, provider.registry.back().version);
    provider.registry[1].contract.packages.front() = compatible("wire", "shared", {1, 8, 0, std::nullopt});
    result = resolve_packages({exact("root-a", "a", {1, 0, 0, std::nullopt}),
                               exact("root-b", "b", {1, 0, 0, std::nullopt})}, provider);
    assert(!result.valid());
    assert(result.issues.front().code == ResolverErrorCode::constraint_conflict);
    assert(result.issues.front().constraints.size() >= 2);
    assert(!result.issues.front().constraints.front().dependencyPath.empty());
  }

  void backtracking_test()
  {
    MemoryProvider provider;
    provider.registry = {
      candidate("a", {1, 9, 0, std::nullopt}, {exact("c", "c", {2, 0, 0, std::nullopt})}),
      candidate("a", {1, 8, 0, std::nullopt}, {exact("c", "c", {1, 5, 0, std::nullopt})}),
      candidate("b", {1, 0, 0, std::nullopt}, {compatible("c", "c", {1, 0, 0, std::nullopt})}),
      candidate("c", {1, 5, 0, std::nullopt}), candidate("c", {2, 0, 0, std::nullopt})};
    const std::vector<PackageRequirement> roots{
      compatible("a-root", "a", {1, 0, 0, std::nullopt}), exact("b-root", "b", {1, 0, 0, std::nullopt})};
    const auto result = resolve_packages(roots, provider);
    assert(result.valid());
    assert(registry_node(*result.value, "a").version == (Version{1, 8, 0, std::nullopt}));
    assert(registry_node(*result.value, "c").version == (Version{1, 5, 0, std::nullopt}));
  }

  void git_tests()
  {
    constexpr const char *repository = "https://example.invalid/protocol";
    MemoryProvider provider;
    provider.git = {
      {repository, TagRevision{"v1.0.0"}, commit_a, digest_a, git_contract()},
      {repository, BranchRevision{"main"}, commit_a, digest_a, git_contract()},
      {repository, CommitRevision{commit_a}, commit_a, digest_a, git_contract()},
      {repository, TagRevision{"v2.0.0"}, commit_b, digest_b, git_contract()}};
    auto result = resolve_packages({git_req("tag", repository, TagRevision{"v1.0.0"})}, provider);
    assert(result.valid() && std::get<ResolvedGitNode>(result.value->nodes.front()).commit == commit_a);
    result = resolve_packages({git_req("branch", repository, BranchRevision{"main"})}, provider);
    assert(result.valid());
    result = resolve_packages({git_req("pin", repository, CommitRevision{commit_a})}, provider);
    assert(result.valid());
    result = resolve_packages({git_req("tag", repository, TagRevision{"v1.0.0"}),
                               git_req("pin", repository, CommitRevision{commit_a})}, provider);
    assert(result.valid() && result.value->nodes.size() == 1);
    result = resolve_packages({git_req("one", repository, TagRevision{"v1.0.0"}),
                               git_req("two", repository, TagRevision{"v2.0.0"})}, provider);
    assert(!result.valid() && result.issues.front().code == ResolverErrorCode::git_commit_conflict);
    result = resolve_packages({git_req("missing", repository, TagRevision{"missing"})}, provider);
    assert(!result.valid() && result.issues.front().code == ResolverErrorCode::git_revision_unresolved);

    auto bad = git_contract();
    bad.identity = RegistryPackageContractIdentity{{"vix", "example", "protocol"}, {1, 0, 0, std::nullopt}};
    provider.git.push_back({repository, TagRevision{"bad-contract"}, commit_a, digest_a, std::move(bad)});
    result = resolve_packages({git_req("bad", repository, TagRevision{"bad-contract"})}, provider);
    assert(!result.valid() && result.issues.front().code == ResolverErrorCode::package_contract_mismatch);

    provider.registry = {candidate("protocol", {1, 0, 0, std::nullopt})};
    result = resolve_packages({exact("registry-protocol", "protocol", {1, 0, 0, std::nullopt}),
                               git_req("git-protocol", repository, TagRevision{"v1.0.0"})}, provider);
    assert(result.valid() && result.value->nodes.size() == 2);
  }

  void provider_and_cycle_tests()
  {
    MemoryProvider provider;
    auto result = resolve_packages({exact("missing", "absent", {1, 0, 0, std::nullopt})}, provider);
    assert(!result.valid() && result.issues.front().code == ResolverErrorCode::no_candidate);

    provider.registry = {
      candidate("zero", {0, 2, 9, std::nullopt}), candidate("zero", {0, 2, 10, std::nullopt}),
      candidate("zero", {0, 3, 0, std::nullopt})};
    result = resolve_packages({compatible("zero", "zero", {0, 2, 3, std::nullopt})}, provider);
    assert(result.valid() && registry_node(*result.value, "zero").version == (Version{0, 2, 10, std::nullopt}));
    provider.registry = {candidate("zero", {0, 0, 3, std::nullopt}), candidate("zero", {0, 0, 4, std::nullopt})};
    result = resolve_packages({compatible("zero", "zero", {0, 0, 3, std::nullopt})}, provider);
    assert(result.valid() && registry_node(*result.value, "zero").version == (Version{0, 0, 3, std::nullopt}));

    provider.registry = {candidate("bad", {1, 0, 0, std::nullopt})};
    provider.registry.front().contract.identity = RegistryPackageContractIdentity{{"vix", "other", "bad"}, {1, 0, 0, std::nullopt}};
    result = resolve_packages({exact("bad", "bad", {1, 0, 0, std::nullopt})}, provider);
    assert(!result.valid() && result.issues.front().code == ResolverErrorCode::package_contract_mismatch);

    provider.registry = {candidate("versioned", {1, 0, 0, std::nullopt})};
    provider.registry.front().contract = registry_contract(provider.registry.front().identity, {2, 0, 0, std::nullopt});
    result = resolve_packages({exact("x", "versioned", {1, 0, 0, std::nullopt})}, provider);
    assert(!result.valid() && result.issues.front().code == ResolverErrorCode::package_contract_mismatch);

    provider.registry = {
      candidate("a", {1, 0, 0, std::nullopt}, {exact("b", "b", {1, 0, 0, std::nullopt})}),
      candidate("b", {1, 0, 0, std::nullopt}, {exact("a", "a", {1, 0, 0, std::nullopt})})};
    result = resolve_packages({exact("root", "a", {1, 0, 0, std::nullopt})}, provider);
    assert(!result.valid() && result.issues.front().code == ResolverErrorCode::dependency_cycle);

    provider.registry = {candidate("dup", {1, 0, 0, std::nullopt}), candidate("dup", {1, 0, 0, std::nullopt})};
    result = resolve_packages({exact("dup", "dup", {1, 0, 0, std::nullopt})}, provider);
    assert(!result.valid() && result.issues.front().code == ResolverErrorCode::invalid_provider_metadata);
  }

  bool same_lock(const Lockfile &left, const Lockfile &right)
  {
    if (left.rootRequirementsDigest != right.rootRequirementsDigest || left.roots.size() != right.roots.size() ||
        left.nodes.size() != right.nodes.size()) return false;
    for (std::size_t index = 0; index < left.roots.size(); ++index)
      if (left.roots[index].alias != right.roots[index].alias || left.roots[index].node != right.roots[index].node) return false;
    for (std::size_t index = 0; index < left.nodes.size(); ++index)
    {
      if (left.nodes[index].index() != right.nodes[index].index()) return false;
      const bool same = std::visit([&](const auto &node)
      {
        using Node = std::decay_t<decltype(node)>;
        const auto *other = std::get_if<Node>(&right.nodes[index]);
        if (!other || node.id != other->id || node.contentDigest != other->contentDigest || node.dependencies.size() != other->dependencies.size()) return false;
        if constexpr (std::is_same_v<Node, RegistryLockNode>) return node.identity == other->identity && node.version == other->version;
        else return node.normalizedRepository == other->normalizedRepository && node.commit == other->commit;
      }, left.nodes[index]);
      if (!same) return false;
    }
    return true;
  }

  void lock_integration_and_determinism_tests()
  {
    MemoryProvider first;
    first.registry = {
      candidate("a", {1, 0, 0, std::nullopt}, {compatible("shared-a", "shared", {1, 0, 0, std::nullopt})}),
      candidate("b", {1, 0, 0, std::nullopt}, {compatible("shared-b", "shared", {1, 1, 0, std::nullopt})}),
      candidate("shared", {1, 1, 0, std::nullopt}), candidate("shared", {1, 5, 0, std::nullopt})};
    MemoryProvider reversed = first;
    std::reverse(reversed.registry.begin(), reversed.registry.end());
    const std::vector<PackageRequirement> roots{
      exact("a-root", "a", {1, 0, 0, std::nullopt}), exact("b-root", "b", {1, 0, 0, std::nullopt})};
    auto graphA = resolve_packages(roots, first);
    auto graphB = resolve_packages({roots[1], roots[0]}, reversed);
    assert(graphA.valid() && graphB.valid());
    const auto lockA = make_resolved_lockfile(*graphA.value);
    const auto lockB = make_resolved_lockfile(*graphB.value);
    assert(lockA.valid() && lockB.valid() && same_lock(*lockA.value, *lockB.value));
    assert(validate_lockfile(*lockA.value).valid());
    assert(validate_lockfile_integrity(*lockA.value).valid());

    const auto textA = serialize_lockfile(*lockA.value);
    const auto textB = serialize_lockfile(*lockB.value);
    assert(textA.valid() && textB.valid() && textA.text == textB.text);
    const auto parsed = parse_lockfile_text(*textA.text, "resolver-lock");
    assert(parsed.valid());
    assert(validate_lockfile(*parsed.value).valid() && validate_lockfile_integrity(*parsed.value).valid());
    assert(same_lock(*lockA.value, *parsed.value));

    MemoryProvider dependenciesForward;
    dependenciesForward.registry = {
      candidate("bundle", {1, 0, 0, std::nullopt},
        {exact("z-dep", "zeta", {1, 0, 0, std::nullopt}), exact("a-dep", "alpha", {1, 0, 0, std::nullopt})}),
      candidate("alpha", {1, 0, 0, std::nullopt}), candidate("zeta", {1, 0, 0, std::nullopt})};
    MemoryProvider dependenciesReversed = dependenciesForward;
    const auto bundle = std::find_if(dependenciesReversed.registry.begin(), dependenciesReversed.registry.end(),
      [](const auto &entry) { return entry.identity.name == "bundle"; });
    std::reverse(bundle->contract.packages.begin(), bundle->contract.packages.end());
    const auto forwardGraph = resolve_packages({exact("bundle", "bundle", {1, 0, 0, std::nullopt})}, dependenciesForward);
    const auto reverseGraph = resolve_packages({exact("bundle", "bundle", {1, 0, 0, std::nullopt})}, dependenciesReversed);
    assert(forwardGraph.valid() && reverseGraph.valid());
    const auto forwardLock = make_resolved_lockfile(*forwardGraph.value);
    const auto reverseLock = make_resolved_lockfile(*reverseGraph.value);
    assert(forwardLock.valid() && reverseLock.valid());
    const auto forwardText = serialize_lockfile(*forwardLock.value);
    const auto reverseText = serialize_lockfile(*reverseLock.value);
    assert(forwardText.valid() && reverseText.valid() && forwardText.text == reverseText.text);

    const auto emptyGraph = resolve_packages({}, first);
    assert(emptyGraph.valid() && emptyGraph.value->roots.empty() && emptyGraph.value->nodes.empty());
    const auto emptyLock = make_resolved_lockfile(*emptyGraph.value);
    assert(!emptyLock.valid() && emptyLock.issues.front().code == ResolverErrorCode::no_root_requirements);
  }
}

int main()
{
  basic_registry_tests();
  transitive_alias_and_conflict_tests();
  backtracking_test();
  git_tests();
  provider_and_cycle_tests();
  lock_integration_and_determinism_tests();
}
