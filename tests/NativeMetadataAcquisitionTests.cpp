#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/Lockfile.hpp>
#include <vix/cli/project/native/MetadataAcquisition.hpp>
#include <vix/cli/project/native/NativeDependencyMutation.hpp>
#include <vix/cli/project/native/Resolver.hpp>
#include <vix/cli/project/native/TomlParser.hpp>
#include <vix/cli/project/native/Validation.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
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

  PackageRequirement exact(std::string alias, RegistryPackageIdentity identity, Version version)
  {
    return {std::move(alias), RegistryRequirement{std::move(identity), ExactVersion{std::move(version)}}};
  }

  PackageRequirement compatible(std::string alias, RegistryPackageIdentity identity, Version version)
  {
    return {std::move(alias), RegistryRequirement{std::move(identity), CompatibleVersion{std::move(version)}}};
  }

  PackageRequirement git_req(std::string alias, std::string repository, GitRevisionRequirement revision)
  {
    return {std::move(alias), GitRequirement{std::move(repository), true, std::move(revision)}};
  }

  PackageContract registry_contract(const RegistryPackageIdentity &identity, Version version,
                                    std::vector<PackageRequirement> packages = {})
  {
    PackageContract contract;
    contract.identity = RegistryPackageContractIdentity{identity, std::move(version)};
    contract.packages = std::move(packages);
    contract.integration = NativeVixIntegration{};
    contract.exports.push_back({"api", NativeExportMapping{"api"}});
    return contract;
  }

  PackageContract git_contract(std::vector<PackageRequirement> packages = {})
  {
    PackageContract contract;
    contract.identity = GitPackageContractIdentity{};
    contract.packages = std::move(packages);
    contract.integration = NativeVixIntegration{};
    contract.exports.push_back({"api", NativeExportMapping{"api"}});
    return contract;
  }

  RegistryCandidate registry(std::string name, Version version, std::vector<PackageRequirement> packages = {},
                             std::string digest = digest_a, std::string registryId = "vix")
  {
    RegistryPackageIdentity identity{std::move(registryId), "example", std::move(name)};
    auto contract = registry_contract(identity, version, std::move(packages));
    return {std::move(identity), std::move(version), std::move(digest), std::move(contract)};
  }

  struct GitCall
  {
    GitPackageIdentity identity;
    GitRevisionRequirement revision;
  };

  struct MemorySource final : PackageMetadataSource
  {
    std::vector<RegistryCandidate> releases;
    std::vector<GitResolution> gitRecords;
    std::vector<RegistryPackageIdentity> registryCalls;
    std::vector<GitCall> gitCalls;
    bool failRegistry{false};
    bool failGit{false};

    RegistryQueryResult registry_releases(const RegistryPackageIdentity &identity) override
    {
      registryCalls.push_back(identity);
      if (failRegistry) return {false, {}, "fixture Registry failure"};
      RegistryQueryResult result;
      for (const auto &release : releases)
        if (release.identity == identity) result.releases.push_back(release);
      return result;
    }

    GitQueryResult resolve_git(const GitPackageIdentity &identity, const GitRevisionRequirement &revision) override
    {
      gitCalls.push_back({identity, revision});
      if (failGit) return {GitQueryStatus::failed, std::nullopt, "fixture Git failure"};
      for (const auto &record : gitRecords)
        if (record.normalizedRepository == identity.normalizedRepository && same_revision(record.requestedRevision, revision))
          return {GitQueryStatus::found, record, {}};
      return {GitQueryStatus::not_found, std::nullopt, {}};
    }
  };

  const ResolvedRegistryNode &registry_node(const ResolvedGraph &graph, const std::string &name)
  {
    const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(), [&](const auto &node)
    {
      const auto *candidate = std::get_if<ResolvedRegistryNode>(&node);
      return candidate && candidate->identity.name == name;
    });
    assert(found != graph.nodes.end());
    return std::get<ResolvedRegistryNode>(*found);
  }

  bool serialized_equal(const Lockfile &left, const Lockfile &right)
  {
    const auto a = serialize_lockfile(left);
    const auto b = serialize_lockfile(right);
    return a.valid() && b.valid() && a.text == b.text;
  }

  void root_and_alias_dedup_tests()
  {
    MemorySource source;
    source.releases = {registry("a", {1, 0, 0, std::nullopt}), registry("a", {1, 1, 0, std::nullopt})};
    const RegistryPackageIdentity a{"vix", "example", "a"};
    const auto acquired = acquire_resolution_catalog({exact("x", a, {1, 0, 0, std::nullopt}),
                                                       compatible("y", a, {1, 0, 0, std::nullopt})}, source);
    assert(acquired.valid());
    assert(source.registryCalls.size() == 1);
    const auto candidates = acquired.catalog->registry_candidates(a);
    assert(candidates.size() == 2);

    MemorySource empty;
    const auto noRoots = acquire_resolution_catalog({}, empty);
    assert(noRoots.valid() && empty.registryCalls.empty() && empty.gitCalls.empty());
  }

  void transitive_and_backtracking_metadata_tests()
  {
    MemorySource source;
    const RegistryPackageIdentity a{"vix", "example", "a"};
    const RegistryPackageIdentity b{"vix", "example", "b"};
    const RegistryPackageIdentity c{"vix", "example", "c"};
    source.releases = {
      registry("a", {1, 9, 0, std::nullopt}, {exact("c-old", c, {2, 0, 0, std::nullopt})}),
      registry("a", {1, 8, 0, std::nullopt}, {exact("c-new", c, {1, 5, 0, std::nullopt})}),
      registry("b", {1, 0, 0, std::nullopt}, {compatible("c-range", c, {1, 0, 0, std::nullopt})}),
      registry("c", {1, 5, 0, std::nullopt}), registry("c", {2, 0, 0, std::nullopt})};
    std::reverse(source.releases.begin(), source.releases.end());
    const auto acquired = acquire_resolution_catalog({compatible("root-a", a, {1, 0, 0, std::nullopt}),
                                                       exact("root-b", b, {1, 0, 0, std::nullopt})}, source);
    assert(acquired.valid());
    assert(source.registryCalls.size() == 3);
    assert(acquired.catalog->registry_candidates(a).size() == 2);
    assert(acquired.catalog->registry_candidates(c).size() == 2);
    const auto graph = resolve_packages({compatible("root-a", a, {1, 0, 0, std::nullopt}),
                                         exact("root-b", b, {1, 0, 0, std::nullopt})}, *acquired.catalog);
    assert(graph.valid());
    assert(registry_node(*graph.value, "a").version == (Version{1, 8, 0, std::nullopt}));
    assert(registry_node(*graph.value, "c").version == (Version{1, 5, 0, std::nullopt}));
  }

  void git_dedup_and_mixed_pipeline_test()
  {
    constexpr const char *repoB = "https://example.invalid/org/b.git";
    constexpr const char *repoD = "https://example.invalid/org/d.git";
    const RegistryPackageIdentity c{"vix", "example", "c"};
    MemorySource source;
    source.releases = {
      registry("a", {1, 0, 0, std::nullopt}, {git_req("b", repoB, TagRevision{"v1"})}),
      registry("c", {2, 0, 0, std::nullopt}, {git_req("d", repoD, BranchRevision{"stable"})})};
    source.gitRecords = {
      {repoB, TagRevision{"v1"}, commit_a, digest_b,
       git_contract({exact("c-local", c, {2, 0, 0, std::nullopt})})},
      {repoD, BranchRevision{"stable"}, commit_b, digest_a, git_contract()}};
    const RegistryPackageIdentity a{"vix", "example", "a"};
    const auto acquired = acquire_resolution_catalog({exact("a-one", a, {1, 0, 0, std::nullopt}),
                                                       exact("a-two", a, {1, 0, 0, std::nullopt})}, source);
    assert(acquired.valid());
    assert(source.registryCalls.size() == 2);
    assert(source.gitCalls.size() == 2);
    const auto graph = resolve_packages({exact("a-one", a, {1, 0, 0, std::nullopt}),
                                         exact("a-two", a, {1, 0, 0, std::nullopt})}, *acquired.catalog);
    assert(graph.valid() && graph.value->nodes.size() == 4);
    const auto lock = make_resolved_lockfile(*graph.value);
    assert(lock.valid() && validate_lockfile(*lock.value).valid());
    assert(validate_lockfile_integrity(*lock.value).valid());
    const auto serialized = serialize_lockfile(*lock.value);
    assert(serialized.valid());
    const auto parsed = parse_lockfile_text(*serialized.text, "metadata-acquisition-pipeline");
    assert(parsed.valid() && validate_lockfile_integrity(*parsed.value).valid());

    MemorySource gitAliases;
    gitAliases.gitRecords = {
      {repoB, TagRevision{"stable"}, commit_a, digest_a, git_contract()},
      {repoB, BranchRevision{"main"}, commit_a, digest_a, git_contract()}};
    const auto declarations = acquire_resolution_catalog({git_req("tag-one", repoB, TagRevision{"stable"}),
                                                           git_req("tag-two", repoB, TagRevision{"stable"}),
                                                           git_req("branch", repoB, BranchRevision{"main"})}, gitAliases);
    assert(declarations.valid());
    assert(gitAliases.gitCalls.size() == 2);
    const auto coalesced = resolve_packages({git_req("tag-one", repoB, TagRevision{"stable"}),
                                             git_req("tag-two", repoB, TagRevision{"stable"}),
                                             git_req("branch", repoB, BranchRevision{"main"})}, *declarations.catalog);
    assert(coalesced.valid() && coalesced.value->nodes.size() == 1);
  }

  void cycle_absence_and_failure_tests()
  {
    const RegistryPackageIdentity a{"vix", "example", "a"};
    const RegistryPackageIdentity b{"vix", "example", "b"};
    MemorySource cycle;
    cycle.releases = {registry("a", {1, 0, 0, std::nullopt}, {exact("b", b, {1, 0, 0, std::nullopt})}),
                      registry("b", {1, 0, 0, std::nullopt}, {exact("a", a, {1, 0, 0, std::nullopt})})};
    const auto acquiredCycle = acquire_resolution_catalog({exact("root", a, {1, 0, 0, std::nullopt})}, cycle);
    assert(acquiredCycle.valid() && cycle.registryCalls.size() == 2);
    const auto cycleGraph = resolve_packages({exact("root", a, {1, 0, 0, std::nullopt})}, *acquiredCycle.catalog);
    assert(!cycleGraph.valid() && cycleGraph.issues.front().code == ResolverErrorCode::dependency_cycle);

    MemorySource absent;
    const RegistryPackageIdentity missing{"vix", "example", "missing"};
    const auto emptyRegistry = acquire_resolution_catalog({exact("missing", missing, {1, 0, 0, std::nullopt})}, absent);
    assert(emptyRegistry.valid());
    const auto noCandidate = resolve_packages({exact("missing", missing, {1, 0, 0, std::nullopt})}, *emptyRegistry.catalog);
    assert(!noCandidate.valid() && noCandidate.issues.front().code == ResolverErrorCode::no_candidate);

    const auto missingGit = acquire_resolution_catalog({git_req("missing", "https://example.invalid/missing.git", TagRevision{"v1"})}, absent);
    assert(missingGit.valid());
    const auto unresolved = resolve_packages({git_req("missing", "https://example.invalid/missing.git", TagRevision{"v1"})}, *missingGit.catalog);
    assert(!unresolved.valid() && unresolved.issues.front().code == ResolverErrorCode::git_revision_unresolved);

    MemorySource invalidCatalogSource;
    auto invalidRelease = registry("a", {1, 0, 0, std::nullopt});
    invalidRelease.contentDigest = "not-a-digest";
    invalidCatalogSource.releases.push_back(std::move(invalidRelease));
    const auto invalidCatalog = acquire_resolution_catalog({exact("a", a, {1, 0, 0, std::nullopt})}, invalidCatalogSource);
    assert(!invalidCatalog.valid());
    assert(invalidCatalog.issues.front().code == MetadataAcquisitionErrorCode::catalog_construction_failure);
    assert(invalidCatalog.issues.front().catalogCode == ResolutionCatalogErrorCode::invalid_content_digest);

    MemorySource registryFailure;
    registryFailure.failRegistry = true;
    const auto failedRegistry = acquire_resolution_catalog({exact("a", a, {1, 0, 0, std::nullopt})}, registryFailure);
    assert(!failedRegistry.valid() && failedRegistry.issues.front().code == MetadataAcquisitionErrorCode::registry_source_failure);
    MemorySource gitFailure;
    gitFailure.failGit = true;
    const auto failedGit = acquire_resolution_catalog({git_req("b", "https://example.invalid/b.git", TagRevision{"v1"})}, gitFailure);
    assert(!failedGit.valid() && failedGit.issues.front().code == MetadataAcquisitionErrorCode::git_source_failure);
  }

  void source_order_independence_test()
  {
    MemorySource first;
    first.releases = {registry("a", {1, 0, 0, std::nullopt}), registry("a", {1, 1, 0, std::nullopt})};
    MemorySource second = first;
    std::reverse(second.releases.begin(), second.releases.end());
    const RegistryPackageIdentity a{"vix", "example", "a"};
    const std::vector<PackageRequirement> roots{compatible("root", a, {1, 0, 0, std::nullopt})};
    const auto acquiredA = acquire_resolution_catalog(roots, first);
    const auto acquiredB = acquire_resolution_catalog(roots, second);
    assert(acquiredA.valid() && acquiredB.valid());
    const auto graphA = resolve_packages(roots, *acquiredA.catalog);
    const auto graphB = resolve_packages(roots, *acquiredB.catalog);
    assert(graphA.valid() && graphB.valid());
    const auto lockA = make_resolved_lockfile(*graphA.value);
    const auto lockB = make_resolved_lockfile(*graphB.value);
    assert(lockA.valid() && lockB.valid() && serialized_equal(*lockA.value, *lockB.value));
  }

  std::string read_text(const std::filesystem::path &path)
  {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  }

  struct FaultingProjectStateOperations final : ProjectStateFileOperations
  {
    int failWriteCall{0};
    std::vector<int> failRenameCalls;
    std::vector<int> failRemoveCalls;
    int writeCalls{0};
    int renameCalls{0};
    int removeCalls{0};
    std::vector<std::filesystem::path> touchedPaths;
    bool is_directory(const std::filesystem::path &path, std::error_code &error) const override
    { return std::filesystem::is_directory(path, error); }
    bool exists(const std::filesystem::path &path, std::error_code &error) const override
    { return std::filesystem::exists(path, error); }
    bool write_complete(const std::filesystem::path &path, std::string_view bytes) override
    {
      touchedPaths.push_back(path);
      if (++writeCalls == failWriteCall)
      {
        std::ofstream partial(path, std::ios::binary | std::ios::trunc);
        partial << "partial";
        return false;
      }
      std::ofstream output(path, std::ios::binary | std::ios::trunc);
      output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      output.flush();
      return output.good();
    }
    void rename(const std::filesystem::path &from, const std::filesystem::path &to,
      std::error_code &error) override
    {
      ++renameCalls;
      touchedPaths.push_back(from);
      touchedPaths.push_back(to);
      if (std::find(failRenameCalls.begin(), failRenameCalls.end(), renameCalls) != failRenameCalls.end())
      {
        error = std::make_error_code(std::errc::permission_denied);
        return;
      }
      std::filesystem::rename(from, to, error);
    }
    void remove(const std::filesystem::path &path, std::error_code &error) override
    {
      ++removeCalls;
      touchedPaths.push_back(path);
      if (std::find(failRemoveCalls.begin(), failRemoveCalls.end(), removeCalls) != failRemoveCalls.end())
      {
        error = std::make_error_code(std::errc::permission_denied);
        return;
      }
      std::filesystem::remove(path, error);
    }
  };

  void assert_project_state_unchanged(const std::filesystem::path &root,
    const std::string &manifest, const std::string &lock)
  {
    assert(read_text(root / "vix.toml") == manifest);
    assert(read_text(root / "vix.lock") == lock);
  }

  void transaction_failure_matrix_tests(const std::filesystem::path &root,
    const std::string &manifest, const std::string &lock)
  {
    const auto ordinary_failure = [&](FaultingProjectStateOperations &operations,
      bool writeManifest = true, std::optional<std::string> candidateLock = std::string{"candidate lock"})
    {
      NativeProjectStateTransaction transaction{root, &operations};
      const auto result = transaction.commit(writeManifest,
        writeManifest ? std::optional<std::string>{"candidate manifest"} : std::nullopt,
        candidateLock);
      assert(!result.valid());
      assert(!result.issues.empty());
      assert(result.issues.front().code != ProjectStateTransactionCode::rollback_failure);
      assert_project_state_unchanged(root, manifest, lock);
    };

    FaultingProjectStateOperations manifestWrite;
    manifestWrite.failWriteCall = 1;
    ordinary_failure(manifestWrite);

    FaultingProjectStateOperations lockWrite;
    lockWrite.failWriteCall = 2;
    ordinary_failure(lockWrite);

    FaultingProjectStateOperations manifestReplace;
    manifestReplace.failRenameCalls = {3};
    ordinary_failure(manifestReplace);

    FaultingProjectStateOperations lockReplace;
    lockReplace.failRenameCalls = {4};
    ordinary_failure(lockReplace);

    FaultingProjectStateOperations lockRemoval;
    lockRemoval.failRemoveCalls = {1};
    ordinary_failure(lockRemoval, true, std::nullopt);

    FaultingProjectStateOperations rollbackFailure;
    rollbackFailure.failRenameCalls = {4, 5};
    NativeProjectStateTransaction transaction{root, &rollbackFailure};
    const auto result = transaction.commit(true, std::string{"candidate manifest"},
      std::string{"candidate lock"});
    assert(!result.valid());
    assert(!result.issues.empty());
    assert(result.issues.front().code == ProjectStateTransactionCode::rollback_failure);
  }

  void native_dependency_mutation_service_tests()
  {
    namespace fs = std::filesystem;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() / ("vix-native-mutation-" + std::to_string(nonce));
    assert(fs::create_directories(root));

    NativeProject project;
    project.name = "mutation-app";
    const auto initial = serialize_native_manifest(project);
    assert(initial.valid());
    {
      std::ofstream output(root / "vix.toml", std::ios::binary);
      output << *initial.text;
      assert(output.good());
    }
    {
      std::ofstream output(root / "vix.lock", std::ios::binary);
      output << "previous lock bytes";
      assert(output.good());
    }
    {
      std::ofstream legacyJson(root / "vix.json", std::ios::binary);
      legacyJson << "not authoritative legacy data";
      std::ofstream legacyApp(root / "vix.app", std::ios::binary);
      legacyApp << "also ignored";
      assert(legacyJson.good() && legacyApp.good());
    }

    MemorySource source;
    source.releases.push_back(registry("a", {1, 2, 0, std::nullopt}));
    NativeDependencyMutationService service{root, source};
    const auto added = service.add(compatible("a", {"vix", "example", "a"}, {1, 0, 0, std::nullopt}));
    assert(added.valid());
    const auto addedManifest = parse_native_manifest_file(root / "vix.toml");
    const auto addedLock = parse_lockfile_file(root / "vix.lock");
    assert(addedManifest.valid() && addedManifest.value->packages.size() == 1);
    assert(addedLock.valid() && addedLock.value->roots.size() == 1);
    const auto duplicate = service.add(compatible("a", {"vix", "example", "a"}, {1, 0, 0, std::nullopt}));
    assert(!duplicate.valid() && duplicate.issues.front().code == NativeDependencyMutationCode::requirement_already_exists);

    const std::string gitRepository{"https://example.invalid/org/git-dependency.git"};
    source.gitRecords.push_back({gitRepository, TagRevision{"v1.0"}, commit_a, digest_b, git_contract()});
    const auto addedGit = service.add(git_req("git-dependency", gitRepository, TagRevision{"v1.0"}));
    assert(addedGit.valid());
    const auto bothLock = parse_lockfile_file(root / "vix.lock");
    assert(bothLock.valid() && bothLock.value->roots.size() == 2 && bothLock.value->nodes.size() == 2);

    const auto beforeUpdateManifest = read_text(root / "vix.toml");
    const auto beforeUpdateLock = read_text(root / "vix.lock");
    source.releases.push_back(registry("a", {1, 4, 0, std::nullopt}));
    FaultingProjectStateOperations updateOperations;
    NativeDependencyMutationService updateService{root, source, &updateOperations};
    const auto updated = updateService.update();
    assert(updated.valid());
    assert(read_text(root / "vix.toml") == beforeUpdateManifest);
    assert(read_text(root / "vix.lock") != beforeUpdateLock);
    assert(std::none_of(updateOperations.touchedPaths.begin(), updateOperations.touchedPaths.end(),
      [](const auto &path) { return path.filename() == "vix.toml"; }));

    const auto currentManifest = read_text(root / "vix.toml");
    {
      std::ofstream output(root / "vix.toml", std::ios::binary | std::ios::trunc);
      output << "# Preserve original formatting on failed update.\n" << currentManifest;
      output.close();
      assert(output.good());
    }
    const auto stableManifest = read_text(root / "vix.toml");
    const auto stableLock = read_text(root / "vix.lock");
    const auto transactionRoot = root / "transaction-matrix";
    assert(fs::create_directories(transactionRoot));
    {
      std::ofstream manifestOutput(transactionRoot / "vix.toml", std::ios::binary);
      manifestOutput << stableManifest;
      std::ofstream lockOutput(transactionRoot / "vix.lock", std::ios::binary);
      lockOutput << stableLock;
      assert(manifestOutput.good() && lockOutput.good());
    }
    transaction_failure_matrix_tests(transactionRoot, stableManifest, stableLock);
    fs::remove_all(transactionRoot);

    // Resolver failure must occur before the transaction begins.
    const auto conflictRoot = root / "resolver-conflict";
    assert(fs::create_directories(conflictRoot));
    {
      std::ofstream output(conflictRoot / "vix.toml", std::ios::binary);
      output << *initial.text;
      assert(output.good());
      std::ofstream lockOutput(conflictRoot / "vix.lock", std::ios::binary);
      lockOutput << "L1";
      assert(lockOutput.good());
    }
    MemorySource conflictSource;
    conflictSource.releases.push_back(registry("a", {1, 2, 0, std::nullopt}));
    FaultingProjectStateOperations noPersistence;
    NativeDependencyMutationService conflictService{conflictRoot, conflictSource, &noPersistence};
    const auto oldManifest = read_text(conflictRoot / "vix.toml");
    const auto oldLock = read_text(conflictRoot / "vix.lock");
    const auto conflict = conflictService.add(compatible("a", {"vix", "example", "a"},
      {2, 0, 0, std::nullopt}));
    assert(!conflict.valid() && conflict.issues.front().code == NativeDependencyMutationCode::resolution_failure);
    assert_project_state_unchanged(conflictRoot, oldManifest, oldLock);
    assert(noPersistence.writeCalls == 0 && noPersistence.renameCalls == 0 && noPersistence.removeCalls == 0);
    fs::remove_all(conflictRoot);

    source.failRegistry = true;
    FaultingProjectStateOperations metadataFailureOperations;
    NativeDependencyMutationService metadataFailureService{root, source, &metadataFailureOperations};
    const auto failedUpdate = metadataFailureService.update();
    assert(!failedUpdate.valid());
    assert(read_text(root / "vix.toml") == stableManifest);
    assert(read_text(root / "vix.lock") == stableLock);
    assert(metadataFailureOperations.writeCalls == 0 && metadataFailureOperations.renameCalls == 0 &&
      metadataFailureOperations.removeCalls == 0);

    const auto removed = service.remove("a");
    assert(removed.valid());
    auto remainingManifest = parse_native_manifest_file(root / "vix.toml");
    assert(remainingManifest.valid() && remainingManifest.value->packages.size() == 1);
    const auto removedGit = service.remove("git-dependency");
    assert(removedGit.valid());
    const auto emptyManifest = parse_native_manifest_file(root / "vix.toml");
    assert(emptyManifest.valid() && emptyManifest.value->packages.empty());
    assert(!fs::exists(root / "vix.lock"));
    fs::remove_all(root);
  }
}

int main()
{
  root_and_alias_dedup_tests();
  transitive_and_backtracking_metadata_tests();
  git_dedup_and_mixed_pipeline_test();
  cycle_absence_and_failure_tests();
  source_order_independence_test();
  native_dependency_mutation_service_tests();
}
