#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/Lockfile.hpp>
#include <vix/cli/project/native/ResolutionCatalog.hpp>
#include <vix/cli/project/native/TomlParser.hpp>
#include <vix/cli/project/native/Validation.hpp>

#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
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

  PackageRequirement exact(std::string alias, RegistryPackageIdentity identity, Version version)
  {
    return {std::move(alias), RegistryRequirement{std::move(identity), ExactVersion{std::move(version)}}};
  }

  PackageRequirement git_req(std::string alias, std::string repository, GitRevisionRequirement revision)
  {
    return {std::move(alias), GitRequirement{std::move(repository), true, std::move(revision)}};
  }

  PackageContract registry_contract(const RegistryPackageIdentity &identity, Version version,
                                    std::vector<PackageRequirement> requirements = {})
  {
    PackageContract contract;
    contract.identity = RegistryPackageContractIdentity{identity, std::move(version)};
    contract.packages = std::move(requirements);
    contract.integration = NativeVixIntegration{};
    contract.exports.push_back({"api", NativeExportMapping{"api"}});
    return contract;
  }

  PackageContract git_contract(std::vector<PackageRequirement> requirements = {})
  {
    PackageContract contract;
    contract.identity = GitPackageContractIdentity{};
    contract.packages = std::move(requirements);
    contract.integration = NativeVixIntegration{};
    contract.exports.push_back({"api", NativeExportMapping{"api"}});
    return contract;
  }

  RegistryCandidate registry(std::string registryId, std::string publisher, std::string name,
                             Version version, std::string digest = digest_a,
                             std::vector<PackageRequirement> requirements = {})
  {
    RegistryPackageIdentity identity{std::move(registryId), std::move(publisher), std::move(name)};
    auto contract = registry_contract(identity, version, std::move(requirements));
    return {std::move(identity), std::move(version), std::move(digest), std::move(contract)};
  }

  bool has_issue(const ResolutionCatalogBuildResult &result, ResolutionCatalogErrorCode code)
  {
    return std::any_of(result.issues.begin(), result.issues.end(), [code](const auto &issue) { return issue.code == code; });
  }

  bool equivalent_locks(const Lockfile &a, const Lockfile &b)
  {
    const auto textA = serialize_lockfile(a);
    const auto textB = serialize_lockfile(b);
    return textA.valid() && textB.valid() && textA.text == textB.text;
  }

  void registry_catalog_tests()
  {
    auto built = build_resolution_catalog({
      registry("vix", "example", "fmt", {1, 0, 0, std::nullopt}),
      registry("vix", "example", "fmt", {2, 0, 0, std::nullopt}),
      registry("corp", "example", "fmt", {1, 0, 0, std::nullopt})}, {});
    assert(built.valid());
    const auto candidates = built.value->registry_candidates({"vix", "example", "fmt"});
    assert(candidates.size() == 2);
    assert(candidates[0].version == (Version{1, 0, 0, std::nullopt}));
    assert(built.value->registry_candidates({"corp", "example", "fmt"}).size() == 1);

    auto mismatch = registry("vix", "example", "fmt", {1, 0, 0, std::nullopt});
    mismatch.contract.identity = RegistryPackageContractIdentity{{"vix", "other", "fmt"}, {1, 0, 0, std::nullopt}};
    built = build_resolution_catalog({mismatch}, {});
    assert(!built.valid() && has_issue(built, ResolutionCatalogErrorCode::registry_contract_identity_mismatch));

    auto versionMismatch = registry("vix", "example", "fmt", {1, 0, 0, std::nullopt});
    versionMismatch.contract = registry_contract(versionMismatch.identity, {1, 1, 0, std::nullopt});
    built = build_resolution_catalog({versionMismatch}, {});
    assert(!built.valid() && has_issue(built, ResolutionCatalogErrorCode::registry_contract_version_mismatch));

    auto first = registry("vix", "example", "fmt", {1, 0, 0, std::nullopt}, digest_a);
    auto contradictory = registry("vix", "example", "fmt", {1, 0, 0, std::nullopt}, digest_b);
    built = build_resolution_catalog({first, contradictory}, {});
    assert(!built.valid() && has_issue(built, ResolutionCatalogErrorCode::conflicting_registry_release));

    built = build_resolution_catalog({first, first}, {});
    assert(!built.valid() && has_issue(built, ResolutionCatalogErrorCode::duplicate_registry_release));

    const RegistryPackageIdentity dependencyIdentity{"vix", "example", "dependency"};
    auto exactContract = registry("vix", "example", "parent", {1, 0, 0, std::nullopt}, digest_a,
      {exact("dep", dependencyIdentity, {1, 2, 3, std::nullopt})});
    auto compatibleContract = registry("vix", "example", "parent", {1, 0, 0, std::nullopt}, digest_a,
      {{"dep", RegistryRequirement{dependencyIdentity, CompatibleVersion{{1, 2, 3, std::nullopt}}}}});
    built = build_resolution_catalog({exactContract, compatibleContract}, {});
    assert(!built.valid() && has_issue(built, ResolutionCatalogErrorCode::conflicting_registry_release));
  }

  void git_catalog_tests()
  {
    constexpr const char *repo = "https://example.invalid/org/protocol.git";
    std::vector<GitResolution> records{
      {repo, CommitRevision{commit_a}, commit_a, digest_a, git_contract()},
      {repo, TagRevision{"v1.2.0"}, commit_a, digest_a, git_contract()},
      {repo, BranchRevision{"main"}, commit_a, digest_a, git_contract()}};
    auto built = build_resolution_catalog({}, records);
    assert(built.valid());
    assert(built.value->git_resolutions(repo, TagRevision{"v1.2.0"}).size() == 1);
    assert(built.value->git_resolutions(repo, CommitRevision{commit_a}).size() == 1);

    auto conflict = records;
    conflict.push_back({repo, TagRevision{"v1.2.0"}, commit_b, digest_b, git_contract()});
    built = build_resolution_catalog({}, conflict);
    assert(!built.valid() && has_issue(built, ResolutionCatalogErrorCode::conflicting_git_revision));

    conflict = {records[0], {repo, TagRevision{"another-tag"}, commit_a, digest_b, git_contract()}};
    built = build_resolution_catalog({}, conflict);
    assert(!built.valid() && has_issue(built, ResolutionCatalogErrorCode::conflicting_git_commit_metadata));

    auto differentContract = git_contract({exact("dep", {"vix", "example", "dep"}, {1, 0, 0, std::nullopt})});
    conflict = {records[0], {repo, TagRevision{"another-tag"}, commit_a, digest_a, differentContract}};
    built = build_resolution_catalog({}, conflict);
    assert(!built.valid() && has_issue(built, ResolutionCatalogErrorCode::conflicting_git_commit_metadata));

    const RegistryPackageIdentity dependencyIdentity{"vix", "example", "dep"};
    auto exactDependency = git_contract({exact("dep", dependencyIdentity, {1, 2, 3, std::nullopt})});
    auto compatibleDependency = git_contract({{"dep", RegistryRequirement{
      dependencyIdentity, CompatibleVersion{{1, 2, 3, std::nullopt}}}}});
    conflict = {records[0], {repo, TagRevision{"different-tag"}, commit_a, digest_a, compatibleDependency}};
    records[0].contract = std::move(exactDependency);
    conflict[0] = records[0];
    built = build_resolution_catalog({}, conflict);
    assert(!built.valid() && has_issue(built, ResolutionCatalogErrorCode::conflicting_git_commit_metadata));

    auto wrongSource = records[0];
    wrongSource.contract = registry_contract({"vix", "example", "protocol"}, {1, 0, 0, std::nullopt});
    built = build_resolution_catalog({}, {wrongSource});
    assert(!built.valid() && has_issue(built, ResolutionCatalogErrorCode::invalid_git_contract_source));

    auto credential = records[0];
    credential.normalizedRepository = "https://user:token@example.invalid/org/protocol.git";
    built = build_resolution_catalog({}, {credential});
    assert(!built.valid() && has_issue(built, ResolutionCatalogErrorCode::invalid_repository_identity));
  }

  void catalog_resolver_integration_tests()
  {
    constexpr const char *repo = "https://example.invalid/org/middle.git";
    const RegistryPackageIdentity aId{"vix", "example", "a"};
    const RegistryPackageIdentity cId{"vix", "example", "c"};
    std::vector<RegistryCandidate> releases{
      registry("vix", "example", "a", {1, 0, 0, std::nullopt}, digest_a,
        {git_req("middle", repo, TagRevision{"v1"})}),
      registry("vix", "example", "c", {2, 0, 0, std::nullopt})};
    std::vector<GitResolution> git{
      {repo, TagRevision{"v1"}, commit_a, digest_b,
       git_contract({exact("leaf", cId, {2, 0, 0, std::nullopt})})}};
    auto catalog = build_resolution_catalog(releases, git);
    assert(catalog.valid());

    // The snapshot owns its values; later mutations of acquisition buffers do not alter it.
    releases.clear();
    git.clear();
    const std::vector<PackageRequirement> roots{
      exact("root-a", aId, {1, 0, 0, std::nullopt})};
    const auto graph = resolve_packages(roots, *catalog.value);
    assert(graph.valid() && graph.value->nodes.size() == 3);
    const auto lock = make_resolved_lockfile(*graph.value);
    assert(lock.valid() && validate_lockfile(*lock.value).valid() && validate_lockfile_integrity(*lock.value).valid());
    const auto serialized = serialize_lockfile(*lock.value);
    assert(serialized.valid());
    const auto parsed = parse_lockfile_text(*serialized.text, "catalog-integration");
    assert(parsed.valid() && validate_lockfile_integrity(*parsed.value).valid());

    PackageContract parsedContract;
    const auto contractResult = parse_package_contract_text(R"toml(
format = 1
[package]
source = "registry"
registry = "vix"
publisher = "example"
name = "parsed"
version = "1.0.0"
[integration]
type = "native-vix"
include-roots = []
[[exports]]
name = "api"
project-target = "api"
)toml");
    assert(contractResult.valid());
    parsedContract = *contractResult.value;
    const auto parsedCandidate = RegistryCandidate{{"vix", "example", "parsed"}, {1, 0, 0, std::nullopt}, digest_a, parsedContract};
    const auto parsedCatalog = build_resolution_catalog({parsedCandidate}, {});
    assert(parsedCatalog.valid());
    const auto parsedGraph = resolve_packages({exact("parsed", parsedCandidate.identity, parsedCandidate.version)}, *parsedCatalog.value);
    assert(parsedGraph.valid());
  }

  void deterministic_snapshot_and_incomplete_tests()
  {
    auto a = registry("vix", "example", "a", {1, 0, 0, std::nullopt});
    auto b = registry("vix", "example", "b", {1, 0, 0, std::nullopt});
    auto one = build_resolution_catalog({a, b}, {});
    auto two = build_resolution_catalog({b, a}, {});
    assert(one.valid() && two.valid());
    const std::vector<PackageRequirement> roots{
      exact("z", {"vix", "example", "b"}, {1, 0, 0, std::nullopt}),
      exact("a", {"vix", "example", "a"}, {1, 0, 0, std::nullopt})};
    const auto graphA = resolve_packages(roots, *one.value);
    const auto graphB = resolve_packages({roots[1], roots[0]}, *two.value);
    assert(graphA.valid() && graphB.valid());
    const auto lockA = make_resolved_lockfile(*graphA.value);
    const auto lockB = make_resolved_lockfile(*graphB.value);
    assert(lockA.valid() && lockB.valid() && equivalent_locks(*lockA.value, *lockB.value));

    const auto missingRegistry = resolve_packages({exact("missing", {"vix", "example", "missing"}, {1, 0, 0, std::nullopt})}, *one.value);
    assert(!missingRegistry.valid() && missingRegistry.issues.front().code == ResolverErrorCode::no_candidate);
    const auto missingGit = resolve_packages({git_req("missing-git", "https://example.invalid/missing.git", TagRevision{"v1"})}, *one.value);
    assert(!missingGit.valid() && missingGit.issues.front().code == ResolverErrorCode::git_revision_unresolved);
  }
}

int main()
{
  registry_catalog_tests();
  git_catalog_tests();
  catalog_resolver_integration_tests();
  deterministic_snapshot_and_incomplete_tests();
}
