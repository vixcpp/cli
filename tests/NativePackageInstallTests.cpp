#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/NativePackageInstall.hpp>
#include <vix/cli/project/native/PackageContentDigest.hpp>
#include <vix/cli/project/native/TomlParser.hpp>
#include <vix/cli/project/native/Validation.hpp>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace vix::cli::project::native;

namespace
{
  namespace fs = std::filesystem;

  struct TempRoot
  {
    fs::path path{fs::temp_directory_path() / ("vix-native-install-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    TempRoot() { assert(fs::create_directories(path)); }
    ~TempRoot() { std::error_code ignored; fs::remove_all(path, ignored); }
  };

  std::string read_file(const fs::path &path)
  {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  }

  void write_file(const fs::path &path, const std::string &bytes)
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    assert(output.good());
  }

  struct CountingRegistrySource final : RegistryPackageContentSource
  {
    int calls{0};
    RegistryPackageContentSourceResult acquire(const RegistryContentLocation &,
      const RegistryPackageContentRequest &) override
    { ++calls; return {}; }
  };

  struct ScriptedRegistrySource final : RegistryPackageContentSource
  {
    std::string canonicalBytes;
    std::optional<RegistryPackageContentRequest> requested;
    int calls{0};
    RegistryPackageContentSourceResult acquire(const RegistryContentLocation &,
      const RegistryPackageContentRequest &request) override
    {
      ++calls;
      requested = request;
      return {RegistryPackageContentSourceStatus::available, canonicalBytes, {}};
    }
  };

  struct CountingGitSource final : GitPackageContentSource
  {
    int calls{0};
    GitPackageContentSourceResult acquire(const GitPackageContentRequest &) override
    { ++calls; return {}; }
  };

  struct ScriptedGitSource final : GitPackageContentSource
  {
    std::shared_ptr<const CanonicalPackageContentCandidate> candidate;
    std::optional<GitPackageContentRequest> requested;
    int calls{0};
    GitPackageContentSourceResult acquire(const GitPackageContentRequest &request) override
    {
      ++calls;
      requested = request;
      return {PackageContentSourceStatus::available, candidate, {}};
    }
  };

  struct ProjectState
  {
    NativeProject project;
    Lockfile lock;
    std::string manifestBytes;
    std::string lockBytes;
    PackageContentId content;
    CanonicalPackageTree tree;
  };

  ProjectState make_project_state()
  {
    NativeProject project;
    project.name = "install-test";
    const RegistryPackageIdentity identity{"vix", "acme", "library"};
    const Version version{1, 2, 3, std::nullopt};
    CanonicalPackageTree tree;
    CanonicalPackageEntry file;
    file.path = "include/value.hpp";
    file.content = {std::byte{0x6f}, std::byte{0x6b}, std::byte{0x0a}};
    tree.entries.push_back(file);
    const auto canonical = canonicalize_package_content_tree(tree);
    assert(canonical.valid());
    const PackageContentId content = *canonical.contentId;
    PackageRequirement requirement{"library", RegistryRequirement{identity, ExactVersion{version}}};
    project.packages.push_back(requirement);
    assert(validate_native_project(project).valid());

    Lockfile lock;
    lock.rootRequirementsDigest = project_requirements_digest(project);
    RegistryLockNode node;
    node.identity = identity;
    node.version = version;
    node.contentDigest = content.digest();
    node.id = registry_node_id(RegistryNodeIdentity{identity, version});
    lock.roots.push_back({requirement.alias,
      NormalizedDeclaredRegistry{identity, std::get<RegistryRequirement>(requirement.source).version}, node.id});
    lock.nodes.emplace_back(node);
    const auto manifest = serialize_native_manifest(project);
    const auto serializedLock = serialize_lockfile(lock);
    assert(manifest.valid() && serializedLock.valid());
    return {std::move(project), std::move(lock), *manifest.text, *serializedLock.text, content, std::move(tree)};
  }

  void no_dependency_no_lock_is_noop()
  {
    TempRoot root;
    NativeProject project;
    project.name = "empty-install";
    const auto manifest = serialize_native_manifest(project);
    assert(manifest.valid());
    write_file(root.path / "vix.toml", *manifest.text);
    const auto registryDirectory = build_registry_directory({});
    assert(registryDirectory.valid());
    FilesystemPackageContentStore store{root.path / "store"};
    CountingRegistrySource registry;
    CountingGitSource git;
    auto result = install_native_project_packages(root.path, AcquisitionMode::online,
      *registryDirectory.value, registry, git, store, root.path / ".vix" / "packages");
    assert(result.valid());
    assert(registry.calls == 0 && git.calls == 0);
    assert(!fs::exists(root.path / "vix.lock"));
  }

  void install_argument_grammar_is_offline_only()
  {
    const auto online = parse_native_install_arguments({});
    assert(online.valid && !online.offline);
    const auto offline = parse_native_install_arguments({"--offline"});
    assert(offline.valid && offline.offline);
    const auto repeated = parse_native_install_arguments({"--offline", "--offline"});
    assert(!repeated.valid && !repeated.diagnostic.empty());
    const auto extra = parse_native_install_arguments({"--offline", "fmt"});
    assert(!extra.valid && !extra.diagnostic.empty());
    const auto package = parse_native_install_arguments({"fmt"});
    const auto url = parse_native_install_arguments({"https://example.invalid/pkg.git"});
    assert(!package.valid && !package.diagnostic.empty());
    assert(!url.valid && !url.diagnostic.empty());
  }

  void offline_reuses_verified_p10p_without_mutating_project()
  {
    TempRoot root;
    const auto state = make_project_state();
    write_file(root.path / "vix.toml", state.manifestBytes);
    write_file(root.path / "vix.lock", state.lockBytes);

    const auto canonical = canonicalize_package_content_tree(state.tree);
    assert(canonical.valid() && canonical.contentId == state.content);
    CanonicalPackageContentVerifier verifier;
    const auto verified = verifier.verify(std::make_shared<CanonicalPackageContentCandidate>(state.tree), state.content);
    assert(verified.valid());
    FilesystemPackageContentStore store{root.path / "store"};
    assert(store.admit(*verified.value).valid());

    const auto registryDirectory = build_registry_directory({});
    assert(registryDirectory.valid());
    CountingRegistrySource registry;
    CountingGitSource git;
    const auto originalManifest = read_file(root.path / "vix.toml");
    const auto originalLock = read_file(root.path / "vix.lock");
    const auto materializationRoot = root.path / ".vix" / "packages";
    auto result = install_native_project_packages(root.path, AcquisitionMode::offline,
      *registryDirectory.value, registry, git, store, materializationRoot);
    assert(result.valid());
    assert(result.content.size() == 1 && result.content.front().state == PackageContentAcquisitionState::reused);
    assert(result.materializations.size() == 1);
    assert(registry.calls == 0 && git.calls == 0);
    assert(read_file(root.path / "vix.toml") == originalManifest);
    assert(read_file(root.path / "vix.lock") == originalLock);
    const auto materialized = result.materializations.front().path;
    assert(materialized && fs::is_regular_file(*materialized / "include" / "value.hpp"));
    assert(read_file(*materialized / "include" / "value.hpp") == "ok\n");

    result = install_native_project_packages(root.path, AcquisitionMode::online,
      *registryDirectory.value, registry, git, store, materializationRoot);
    assert(result.valid());
    assert(result.content.size() == 1 && result.content.front().state == PackageContentAcquisitionState::reused);
    assert(registry.calls == 0 && git.calls == 0);
    assert(read_file(root.path / "vix.toml") == originalManifest);
    assert(read_file(root.path / "vix.lock") == originalLock);
  }

  void missing_lock_fails_without_source_calls()
  {
    TempRoot root;
    auto state = make_project_state();
    const auto manifest = serialize_native_manifest(state.project);
    assert(manifest.valid());
    write_file(root.path / "vix.toml", *manifest.text);
    const auto registryDirectory = build_registry_directory({});
    assert(registryDirectory.valid());
    FilesystemPackageContentStore store{root.path / "store"};
    CountingRegistrySource registry;
    CountingGitSource git;
    const auto result = install_native_project_packages(root.path, AcquisitionMode::online,
      *registryDirectory.value, registry, git, store, root.path / ".vix" / "packages");
    assert(!result.valid() && result.issues.front().code == NativePackageInstallIssueCode::lock_missing);
    assert(registry.calls == 0 && git.calls == 0);
    assert(!fs::exists(root.path / "vix.lock"));
  }

  void offline_missing_content_fails_before_sources_and_preserves_files()
  {
    TempRoot root;
    const auto state = make_project_state();
    write_file(root.path / "vix.toml", state.manifestBytes);
    write_file(root.path / "vix.lock", state.lockBytes);
    const auto registryDirectory = build_registry_directory({});
    assert(registryDirectory.valid());
    FilesystemPackageContentStore store{root.path / "store"};
    CountingRegistrySource registry;
    CountingGitSource git;
    const auto result = install_native_project_packages(root.path, AcquisitionMode::offline,
      *registryDirectory.value, registry, git, store, root.path / ".vix" / "packages");
    assert(!result.valid() && result.issues.front().code ==
      NativePackageInstallIssueCode::offline_content_missing);
    assert(result.issues.front().message.find("offline mode prohibits package source acquisition") != std::string::npos);
    assert(registry.calls == 0 && git.calls == 0);
    assert(read_file(root.path / "vix.toml") == state.manifestBytes);
    assert(read_file(root.path / "vix.lock") == state.lockBytes);
    assert(!fs::exists(root.path / ".vix" / "packages"));
  }

  void offline_corrupt_cached_content_fails_closed()
  {
    TempRoot root;
    const auto state = make_project_state();
    write_file(root.path / "vix.toml", state.manifestBytes);
    write_file(root.path / "vix.lock", state.lockBytes);

    const auto canonical = canonicalize_package_content_tree(state.tree);
    assert(canonical.valid());
    CanonicalPackageContentVerifier verifier;
    const auto verified = verifier.verify(
      std::make_shared<CanonicalPackageContentCandidate>(state.tree), state.content);
    assert(verified.valid());
    const fs::path storePath = root.path / "store";
    FilesystemPackageContentStore store{storePath};
    assert(store.admit(*verified.value).valid());
    const std::string hex = state.content.digest().substr(7);
    write_file(storePath / "objects" / "sha256" / hex.substr(0, 2) / hex.substr(2), "corrupted object");

    const auto registryDirectory = build_registry_directory({});
    assert(registryDirectory.valid());
    CountingRegistrySource registry;
    CountingGitSource git;
    const auto result = install_native_project_packages(root.path, AcquisitionMode::offline,
      *registryDirectory.value, registry, git, store, root.path / ".vix" / "packages");
    assert(!result.valid() && result.issues.front().code ==
      NativePackageInstallIssueCode::corrupt_cached_content);
    assert(result.issues.front().message.find("corrupt") != std::string::npos);
    assert(registry.calls == 0 && git.calls == 0);
    assert(!fs::exists(root.path / ".vix" / "packages"));
  }

  void stale_lock_is_rejected_before_sources()
  {
    TempRoot root;
    auto state = make_project_state();
    state.project.packages.push_back({"other", RegistryRequirement{{"vix", "acme", "other"},
      ExactVersion{Version{1, 0, 0, std::nullopt}}}});
    const auto manifest = serialize_native_manifest(state.project);
    assert(manifest.valid());
    write_file(root.path / "vix.toml", *manifest.text);
    write_file(root.path / "vix.lock", state.lockBytes);
    const auto registryDirectory = build_registry_directory({});
    assert(registryDirectory.valid());
    FilesystemPackageContentStore store{root.path / "store"};
    CountingRegistrySource registry;
    CountingGitSource git;
    const auto result = install_native_project_packages(root.path, AcquisitionMode::online,
      *registryDirectory.value, registry, git, store, root.path / ".vix" / "packages");
    assert(!result.valid() && result.issues.front().code == NativePackageInstallIssueCode::lock_stale);
    assert(registry.calls == 0 && git.calls == 0);
    assert(read_file(root.path / "vix.toml") == *manifest.text);
    assert(read_file(root.path / "vix.lock") == state.lockBytes);
  }

  void invalid_lock_is_rejected_before_sources()
  {
    TempRoot root;
    const auto state = make_project_state();
    write_file(root.path / "vix.toml", state.manifestBytes);
    write_file(root.path / "vix.lock", "not a format-1 lock\n");
    const auto registryDirectory = build_registry_directory({});
    assert(registryDirectory.valid());
    FilesystemPackageContentStore store{root.path / "store"};
    CountingRegistrySource registry;
    CountingGitSource git;
    const auto result = install_native_project_packages(root.path, AcquisitionMode::online,
      *registryDirectory.value, registry, git, store, root.path / ".vix" / "packages");
    assert(!result.valid() && result.issues.front().code == NativePackageInstallIssueCode::lock_invalid);
    assert(registry.calls == 0 && git.calls == 0);
  }

  void online_registry_content_uses_exact_lock_and_admits_p10p()
  {
    TempRoot root;
    const auto state = make_project_state();
    write_file(root.path / "vix.toml", state.manifestBytes);
    write_file(root.path / "vix.lock", state.lockBytes);
    const auto directory = build_registry_directory({RegistryDefinition{"vix", {{"file:///mirror"}},
      {{"file:///mirror"}}}});
    assert(directory.valid());
    const auto canonical = canonicalize_package_content_tree(state.tree);
    assert(canonical.valid());
    ScriptedRegistrySource registry;
    registry.canonicalBytes = *canonical.canonicalBytes;
    CountingGitSource git;
    FilesystemPackageContentStore store{root.path / "store"};
    const auto originalManifest = read_file(root.path / "vix.toml");
    const auto originalLock = read_file(root.path / "vix.lock");
    const auto result = install_native_project_packages(root.path, AcquisitionMode::online,
      *directory.value, registry, git, store, root.path / ".vix" / "packages");
    assert(result.valid());
    assert(registry.calls == 1 && git.calls == 0);
    assert(registry.requested && registry.requested->identity == (RegistryPackageIdentity{"vix", "acme", "library"}));
    assert(registry.requested->version == (Version{1, 2, 3, std::nullopt}));
    assert(registry.requested->expectedContent == state.content);
    assert(store.inspect(state.content).value == PackageContentAvailability::available);
    assert(read_file(root.path / "vix.toml") == originalManifest);
    assert(read_file(root.path / "vix.lock") == originalLock);
  }

  void online_git_content_uses_exact_locked_commit()
  {
    TempRoot root;
    const std::string repository{"https://example.invalid/acme/library.git"};
    const std::string commit{"0123456789abcdef0123456789abcdef01234567"};
    CanonicalPackageTree tree;
    CanonicalPackageEntry file;
    file.path = "library.hpp";
    file.content = {std::byte{0x67}, std::byte{0x69}, std::byte{0x74}};
    tree.entries.push_back(file);
    const auto canonical = canonicalize_package_content_tree(tree);
    assert(canonical.valid());
    NativeProject project;
    project.name = "git-install";
    const PackageRequirement requirement{"library", GitRequirement{repository, true, CommitRevision{commit}}};
    project.packages.push_back(requirement);
    assert(validate_native_project(project).valid());
    Lockfile lock;
    lock.rootRequirementsDigest = project_requirements_digest(project);
    GitLockNode node;
    node.normalizedRepository = repository;
    node.commit = commit;
    node.contentDigest = canonical.contentId->digest();
    node.id = git_node_id(GitNodeIdentity{repository, commit});
    lock.roots.push_back({requirement.alias, NormalizedDeclaredGit{repository, true,
      std::get<GitRequirement>(requirement.source).revision}, node.id});
    lock.nodes.emplace_back(node);
    const auto manifest = serialize_native_manifest(project);
    const auto lockText = serialize_lockfile(lock);
    assert(manifest.valid() && lockText.valid());
    write_file(root.path / "vix.toml", *manifest.text);
    write_file(root.path / "vix.lock", *lockText.text);

    const auto directory = build_registry_directory({});
    assert(directory.valid());
    CountingRegistrySource registry;
    ScriptedGitSource git;
    git.candidate = std::make_shared<CanonicalPackageContentCandidate>(tree);
    FilesystemPackageContentStore store{root.path / "store"};
    const auto originalManifest = read_file(root.path / "vix.toml");
    const auto originalLock = read_file(root.path / "vix.lock");
    const auto result = install_native_project_packages(root.path, AcquisitionMode::online,
      *directory.value, registry, git, store, root.path / ".vix" / "packages");
    assert(result.valid());
    assert(registry.calls == 0 && git.calls == 1);
    assert(git.requested && git.requested->normalizedRepository == repository);
    assert(git.requested->fullCommit == commit && git.requested->expectedContent == *canonical.contentId);
    assert(store.inspect(*canonical.contentId).value == PackageContentAvailability::available);
    assert(read_file(root.path / "vix.toml") == originalManifest);
    assert(read_file(root.path / "vix.lock") == originalLock);
  }
}

int main()
{
  install_argument_grammar_is_offline_only();
  no_dependency_no_lock_is_noop();
  offline_reuses_verified_p10p_without_mutating_project();
  missing_lock_fails_without_source_calls();
  offline_missing_content_fails_before_sources_and_preserves_files();
  offline_corrupt_cached_content_fails_closed();
  stale_lock_is_rejected_before_sources();
  invalid_lock_is_rejected_before_sources();
  online_registry_content_uses_exact_lock_and_admits_p10p();
  online_git_content_uses_exact_locked_commit();
}
