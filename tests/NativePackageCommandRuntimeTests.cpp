#include <vix/cli/project/native/PackageCommandRuntime.hpp>
#include <vix/cli/project/native/NativeDependencyMutation.hpp>
#include <vix/cli/project/native/Lockfile.hpp>
#include <vix/cli/project/native/TomlParser.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

using namespace vix::cli::project::native;

namespace
{
  void set_environment(const char *name, const std::string &value)
  {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
  }

  void clear_environment(const char *name)
  {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
  }

  std::string write_config(const std::filesystem::path &path, const std::string &text)
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    output.close();
    assert(output.good());
    return path.string();
  }

  constexpr const char *content_digest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  constexpr const char *git_commit = "0123456789abcdef0123456789abcdef01234567";

  bool same_revision(const GitRevisionRequirement &left, const GitRevisionRequirement &right)
  {
    if (left.index() != right.index()) return false;
    return std::visit([&](const auto &value)
    {
      const auto *other = std::get_if<std::decay_t<decltype(value)>>(&right);
      return other && value.value == other->value;
    }, left);
  }

  PackageContract registry_contract(const RegistryPackageIdentity &identity, Version version)
  {
    PackageContract contract;
    contract.identity = RegistryPackageContractIdentity{identity, std::move(version)};
    contract.integration = NativeVixIntegration{};
    contract.exports.push_back({"api", NativeExportMapping{"api"}});
    return contract;
  }

  struct CommandMetadataSource final : PackageMetadataSource
  {
    std::vector<RegistryCandidate> releases;
    std::optional<GitResolution> git;
    int registryCalls{0};
    int gitCalls{0};
    RegistryQueryResult registry_releases(const RegistryPackageIdentity &identity) override
    {
      ++registryCalls;
      RegistryQueryResult result;
      for (const auto &release : releases)
        if (release.identity == identity) result.releases.push_back(release);
      return result;
    }
    GitQueryResult resolve_git(const GitPackageIdentity &identity,
      const GitRevisionRequirement &revision) override
    {
      ++gitCalls;
      if (!git || git->normalizedRepository != identity.normalizedRepository ||
          !same_revision(git->requestedRevision, revision))
        return {GitQueryStatus::not_found, std::nullopt, "scripted Git revision missing"};
      return {GitQueryStatus::found, git, {}};
    }
  };

  struct CommandGitDefaults final : GitDefaultBranchSource
  {
    std::string branch{"main"};
    int calls{0};
    bool fail{false};
    GitDefaultBranchResult discover_default_branch(const GitPackageIdentity &) override
    { ++calls; return fail ? GitDefaultBranchResult{std::nullopt, "no symbolic HEAD"} :
        GitDefaultBranchResult{branch, {}}; }
  };

  std::string read_file(const std::filesystem::path &path)
  {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  }

  void command_parser_tests()
  {
    auto parsed = parse_native_add_arguments({"fmt/fmt", "--version", "compatible=10.2.0"});
    assert(parsed.valid());
    assert(parsed.value->requirement);
    const auto &registry = std::get<RegistryRequirement>(parsed.value->requirement->source);
    assert(registry.identity.registry == "vix" && registry.identity.publisher == "fmt" &&
      registry.identity.name == "fmt");

    parsed = parse_native_add_arguments({"acme/math", "--registry", "corp", "--version", "exact=2.1.0"});
    assert(parsed.valid());
    assert(std::get<RegistryRequirement>(parsed.value->requirement->source).identity.registry == "corp");
    parsed = parse_native_add_arguments({"acme/math", "--registry", "corp"});
    assert(parsed.valid() && parsed.value->implicitStableRegistry &&
      parsed.value->implicitStableRegistry->registry == "corp");
    parsed = parse_native_add_arguments({"fmt/fmt"});
    assert(parsed.valid() && parsed.value->implicitStableRegistry &&
      parsed.value->implicitStableRegistry->registry == "vix");
    assert(!parse_native_add_arguments({"bad//name", "--version", "exact=1.0.0"}).valid());
    assert(!parse_native_add_arguments({"fmt/fmt", "--version", "nonsense=1.0.0"}).valid());

    parsed = parse_native_add_arguments({"https://example.org/acme/math.git", "--commit",
      "0123456789abcdef0123456789abcdef01234567"});
    assert(parsed.valid());
    assert(parsed.value->requirement && std::holds_alternative<GitRequirement>(parsed.value->requirement->source));
    parsed = parse_native_add_arguments({"git@example.org:acme/math.git", "--branch", "release/1.x"});
    assert(parsed.valid());
    parsed = parse_native_add_arguments({"git@example.org:acme/math.git"});
    assert(parsed.valid() && parsed.value->implicitDefaultBranch);
    parsed = parse_native_add_arguments({"https://example.org/acme/math.git", "--tag", "v1.2"});
    assert(parsed.valid());
    parsed = parse_native_add_arguments({"https://example.org/acme/math.git"});
    assert(parsed.valid() && parsed.value->implicitDefaultBranch);
    assert(!parse_native_add_arguments({"https://example.org/acme/math.git", "--branch", "main",
      "--tag", "v1"}).valid());
    assert(!parse_native_add_arguments({"owner/repo", "--branch", "main"}).valid());

    auto removal = parse_native_remove_arguments({"fmt/fmt"});
    assert(removal.valid() && std::get<RegistryPackageIdentity>(removal.value->identity).registry == "vix");
    removal = parse_native_remove_arguments({"fmt/fmt", "--registry", "corp"});
    assert(removal.valid() && std::get<RegistryPackageIdentity>(removal.value->identity).registry == "corp");
    removal = parse_native_remove_arguments({"https://example.org/acme/math.git"});
    assert(removal.valid() && std::holds_alternative<GitPackageIdentity>(removal.value->identity));
    assert(!parse_native_remove_arguments({"fmt/fmt", "--version", "exact=1.0.0"}).valid());
    assert(parse_native_update_arguments({}).valid());
    assert(!parse_native_update_arguments({"fmt/fmt"}).valid());
  }

  void runtime_configuration_tests()
  {
    namespace fs = std::filesystem;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() / ("vix-runtime-config-" + std::to_string(nonce));
    assert(fs::create_directories(root));
    const auto valid = R"toml(format = 1

[[registry]]
id = "vix"
authority-id = "root"
locations = ["https://mirror.example", "file:///tmp/mirror"]

[[registry]]
id = "corp"
authority-id = "root"
locations = ["https://corp.example/"]

[[trusted-key]]
authority-id = "root"
key-id = "key-a"
algorithm = "ed25519"
public-key = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

[[trusted-key]]
authority-id = "root"
key-id = "key-b"
algorithm = "ed25519"
public-key = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
)toml";
    const auto path = fs::path{write_config(root / "registries.toml", valid)};
    auto result = load_registry_runtime_configuration(path);
    assert(result.valid());
    assert(result.configuration->registries.size() == 2);
    assert(result.configuration->registries[0].id == "vix");
    assert(result.configuration->registries[0].locations.size() == 2);
    assert(result.configuration->registries[0].locations[0].descriptor == "https://mirror.example");
    assert(result.configuration->authorities.size() == 2);
    assert(result.configuration->authorities[0].registryId == "vix" &&
      result.configuration->authorities[0].authorityId == "root");
    assert(result.configuration->trustedKeys.size() == 1);
    assert(result.configuration->trustedKeys[0].keys.size() == 2);
    std::string runtimeDiagnostic;
    auto configuredRuntime = OnlinePackageMetadataRuntime::create(*result.configuration,
      root / "metadata-configured", root / "git-configured", runtimeDiagnostic);
    assert(configuredRuntime && configuredRuntime->has_registry("vix") &&
      configuredRuntime->has_registry("corp"));

    auto malformed = load_registry_runtime_configuration(write_config(root / "duplicate.toml",
      "format=1\n[[registry]]\nid='vix'\nauthority-id='a'\nlocations=['https://a']\n"
      "[[registry]]\nid='vix'\nauthority-id='b'\nlocations=['https://b']\n"));
    assert(!malformed.valid());
    malformed = load_registry_runtime_configuration(write_config(root / "unsupported-format.toml", "format=2\n"));
    assert(!malformed.valid());
    malformed = load_registry_runtime_configuration(write_config(root / "duplicate-key.toml",
      "format=1\n[[trusted-key]]\nauthority-id='a'\nkey-id='k'\nalgorithm='ed25519'\npublic-key='" +
      std::string(64, 'a') + "'\n[[trusted-key]]\nauthority-id='a'\nkey-id='k'\nalgorithm='ed25519'\npublic-key='" +
      std::string(64, 'b') + "'\n"));
    assert(!malformed.valid());
    malformed = load_registry_runtime_configuration(write_config(root / "bad-key.toml",
      "format=1\n[[trusted-key]]\nauthority-id='a'\nkey-id='k'\nalgorithm='ed25519'\npublic-key='bad'\n"));
    assert(!malformed.valid());
    malformed = load_registry_runtime_configuration(write_config(root / "bad-algorithm.toml",
      "format=1\n[[trusted-key]]\nauthority-id='a'\nkey-id='k'\nalgorithm='rsa'\npublic-key='" +
      std::string(64, 'a') + "'\n"));
    assert(!malformed.valid());
    malformed = load_registry_runtime_configuration(write_config(root / "http.toml",
      "format=1\n[[registry]]\nid='vix'\nauthority-id='a'\nlocations=['http://example.org']\n"));
    assert(!malformed.valid());
    malformed = load_registry_runtime_configuration(write_config(root / "unknown-scheme.toml",
      "format=1\n[[registry]]\nid='vix'\nauthority-id='a'\nlocations=['ftp://example.org']\n"));
    assert(!malformed.valid());
    malformed = load_registry_runtime_configuration(write_config(root / "remote-file.toml",
      "format=1\n[[registry]]\nid='vix'\nauthority-id='a'\nlocations=['file://host/path']\n"));
    assert(!malformed.valid());

    const auto missing = load_registry_runtime_configuration(root / "missing.toml");
    assert(missing.valid() && missing.configuration->registries.empty());
    const auto oldConfig = std::getenv("XDG_CONFIG_HOME") ? std::optional<std::string>{std::getenv("XDG_CONFIG_HOME")} : std::nullopt;
    const auto oldState = std::getenv("XDG_STATE_HOME") ? std::optional<std::string>{std::getenv("XDG_STATE_HOME")} : std::nullopt;
    const auto oldHome = std::getenv("HOME") ? std::optional<std::string>{std::getenv("HOME")} : std::nullopt;
    const auto oldOverride = std::getenv("VIX_REGISTRY_CONFIG") ? std::optional<std::string>{std::getenv("VIX_REGISTRY_CONFIG")} : std::nullopt;
    set_environment("HOME", root.string());
    set_environment("XDG_CONFIG_HOME", (root / "xdg-config").string());
    set_environment("XDG_STATE_HOME", (root / "xdg-state").string());
    clear_environment("VIX_REGISTRY_CONFIG");
    assert(registry_runtime_configuration_path() == root / "xdg-config/vix/registries.toml");
    assert(metadata_state_path() == root / "xdg-state/vix/metadata");
    set_environment("VIX_REGISTRY_CONFIG", path.string());
    assert(registry_runtime_configuration_path() == path);
    clear_environment("VIX_REGISTRY_CONFIG");
    set_environment("XDG_CONFIG_HOME", "");
    assert(registry_runtime_configuration_path() == root / ".config/vix/registries.toml");
    set_environment("XDG_STATE_HOME", "");
    assert(metadata_state_path() == root / ".local/state/vix/metadata");
    if (oldConfig) set_environment("XDG_CONFIG_HOME", *oldConfig); else clear_environment("XDG_CONFIG_HOME");
    if (oldState) set_environment("XDG_STATE_HOME", *oldState); else clear_environment("XDG_STATE_HOME");
    if (oldHome) set_environment("HOME", *oldHome); else clear_environment("HOME");
    if (oldOverride) set_environment("VIX_REGISTRY_CONFIG", *oldOverride); else clear_environment("VIX_REGISTRY_CONFIG");

    RegistryRuntimeConfiguration empty;
    std::string diagnostic;
    auto runtime = OnlinePackageMetadataRuntime::create(empty, root / "metadata", root / "git", diagnostic);
    assert(runtime && !runtime->has_registry("vix"));
    fs::remove_all(root);
  }

  void command_flow_tests()
  {
    namespace fs = std::filesystem;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() / ("vix-command-flow-" + std::to_string(nonce));
    assert(fs::create_directories(root));
    NativeProject project;
    project.name = "command-flow";
    const auto manifest = serialize_native_manifest(project);
    assert(manifest.valid());
    {
      std::ofstream output(root / "vix.toml", std::ios::binary);
      output << *manifest.text;
      std::ofstream legacy(root / "vix.json", std::ios::binary);
      legacy << "ignored";
      std::ofstream legacyApp(root / "vix.app", std::ios::binary);
      legacyApp << "ignored app";
      assert(output.good() && legacy.good() && legacyApp.good());
    }

    CommandMetadataSource source;
    const RegistryPackageIdentity identity{"vix", "fmt", "fmt"};
    Version registryVersion{1, 2, 0, std::nullopt};
    source.releases.push_back({identity, registryVersion, content_digest,
      registry_contract(identity, registryVersion)});
    assert(run_native_add_command_with_source({"fmt/fmt", "--version", "compatible=1.0.0"}, root, source) == 0);
    auto resolvedLock = read_file(root / "vix.lock");
    assert(!resolvedLock.empty() && source.registryCalls == 1);
    const auto manifestAfterRegistryAdd = read_file(root / "vix.toml");
    const auto failedAdd = run_native_add_command_with_source(
      {"missing/pkg", "--version", "exact=1.0.0"}, root, source);
    assert(failedAdd != 0);
    assert(read_file(root / "vix.toml") == manifestAfterRegistryAdd);
    assert(read_file(root / "vix.lock") == resolvedLock);

    const RegistryPackageIdentity inferredIdentity{"vix", "acme", "math"};
    const Version stableOlder{1, 0, 0, std::nullopt};
    const Version stableNewest{1, 4, 0, std::nullopt};
    const Version prereleaseNewer{2, 0, 0, std::string{"beta.1"}};
    source.releases.push_back({inferredIdentity, stableOlder, content_digest,
      registry_contract(inferredIdentity, stableOlder)});
    source.releases.push_back({inferredIdentity, prereleaseNewer, content_digest,
      registry_contract(inferredIdentity, prereleaseNewer)});
    source.releases.push_back({inferredIdentity, stableNewest, content_digest,
      registry_contract(inferredIdentity, stableNewest)});
    assert(run_native_add_command_with_source({"acme/math"}, root, source) == 0);
    auto inferredManifest = parse_native_manifest_file(root / "vix.toml");
    assert(inferredManifest.valid());
    const auto &inferredRequirement = std::get<RegistryRequirement>(inferredManifest.value->packages.back().source);
    assert(inferredRequirement.identity == inferredIdentity);
    const auto &inferredVersion = std::get<CompatibleVersion>(inferredRequirement.version).version;
    assert(inferredVersion == stableNewest);

    const auto beforeNoStableManifest = read_file(root / "vix.toml");
    const auto beforeNoStableLock = read_file(root / "vix.lock");
    const RegistryPackageIdentity noStableIdentity{"vix", "acme", "preview-only"};
    const Version onlyPrerelease{9, 0, 0, std::string{"rc.1"}};
    source.releases.push_back({noStableIdentity, onlyPrerelease, content_digest,
      registry_contract(noStableIdentity, onlyPrerelease)});
    assert(run_native_add_command_with_source({"acme/preview-only"}, root, source) != 0);
    assert(read_file(root / "vix.toml") == beforeNoStableManifest);
    assert(read_file(root / "vix.lock") == beforeNoStableLock);

    const auto beforeGitDiscoveryFailureManifest = read_file(root / "vix.toml");
    const auto beforeGitDiscoveryFailureLock = read_file(root / "vix.lock");
    CommandGitDefaults missingGitDefaults;
    missingGitDefaults.fail = true;
    assert(run_native_add_command_with_source({"https://example.invalid/org/unavailable.git"},
      root, source, &missingGitDefaults) != 0);
    assert(missingGitDefaults.calls == 1);
    assert(read_file(root / "vix.toml") == beforeGitDiscoveryFailureManifest);
    assert(read_file(root / "vix.lock") == beforeGitDiscoveryFailureLock);

    const std::string repository{"https://example.invalid/org/math.git"};
    const GitRevisionRequirement branch{BranchRevision{"main"}};
    PackageContract gitContract;
    gitContract.identity = GitPackageContractIdentity{};
    gitContract.integration = NativeVixIntegration{};
    gitContract.exports.push_back({"api", NativeExportMapping{"api"}});
    source.git = GitResolution{repository, branch, git_commit, content_digest, gitContract};
    CommandGitDefaults gitDefaults;
    gitDefaults.branch = "trunk";
    const GitRevisionRequirement trunk{BranchRevision{"trunk"}};
    source.git->requestedRevision = trunk;
    assert(run_native_add_command_with_source({repository}, root, source, &gitDefaults) == 0);
    assert(gitDefaults.calls == 1);
    const auto manifestAfterBothAdds = read_file(root / "vix.toml");
    assert(manifestAfterBothAdds != manifestAfterRegistryAdd);
    auto afterImplicitGit = parse_native_manifest_file(root / "vix.toml");
    assert(afterImplicitGit.valid());
    const auto gitRoot = std::find_if(afterImplicitGit.value->packages.begin(), afterImplicitGit.value->packages.end(),
      [](const auto &requirement) { return std::holds_alternative<GitRequirement>(requirement.source); });
    assert(gitRoot != afterImplicitGit.value->packages.end());
    assert(std::get<BranchRevision>(std::get<GitRequirement>(gitRoot->source).revision).value == "trunk");
    const int discoveriesBeforeOverrides = gitDefaults.calls;
    const std::vector<std::pair<std::vector<std::string>, GitRevisionRequirement>> overrides{
      {{repository, "--branch", "main"}, BranchRevision{"main"}},
      {{repository, "--tag", "v1.0"}, TagRevision{"v1.0"}},
      {{repository, "--commit", git_commit}, CommitRevision{git_commit}}
    };
    std::size_t overrideIndex = 0;
    for (const auto &[arguments, revision] : overrides)
    {
      const auto overrideRoot = root / ("explicit-git-" + std::to_string(overrideIndex++));
      assert(fs::create_directories(overrideRoot));
      std::ofstream output(overrideRoot / "vix.toml", std::ios::binary);
      output << *manifest.text;
      output.close();
      assert(output.good());
      source.git->requestedRevision = revision;
      assert(run_native_add_command_with_source(arguments, overrideRoot, source, &gitDefaults) == 0);
    }
    assert(gitDefaults.calls == discoveriesBeforeOverrides);
    source.git->requestedRevision = trunk;
    const auto beforeUpdateLock = read_file(root / "vix.lock");

    registryVersion = {1, 5, 0, std::nullopt};
    source.releases.push_back({identity, registryVersion, content_digest,
      registry_contract(identity, registryVersion)});
    const std::string updatedCommit{"1123456789abcdef0123456789abcdef01234567"};
    source.git->commit = updatedCommit;
    assert(run_native_update_command_with_source({}, root, source) == 0);
    assert(read_file(root / "vix.toml") == manifestAfterBothAdds);
    assert(read_file(root / "vix.lock") != beforeUpdateLock);

    assert(run_native_remove_command_with_source({"fmt/fmt"}, root, source) == 0);
    auto remaining = parse_native_manifest_file(root / "vix.toml");
    assert(remaining.valid() && remaining.value->packages.size() == 2);
    const auto hasRegistryRoot = [&](const RegistryPackageIdentity &expected)
    {
      return std::any_of(remaining.value->packages.begin(), remaining.value->packages.end(),
        [&](const PackageRequirement &requirement)
        {
          const auto *registry = std::get_if<RegistryRequirement>(&requirement.source);
          return registry && registry->identity == expected;
        });
    };
    const auto hasGitRoot = [&](const std::string &expectedRepository)
    {
      return std::any_of(remaining.value->packages.begin(), remaining.value->packages.end(),
        [&](const PackageRequirement &requirement)
        {
          const auto *git = std::get_if<GitRequirement>(&requirement.source);
          return git && git->normalizedRepository == expectedRepository;
        });
    };
    assert(!hasRegistryRoot(identity));
    assert(hasRegistryRoot(inferredIdentity));
    assert(hasGitRoot(repository));
    const int sourceCallsBeforeGitRemove = source.registryCalls + source.gitCalls;
    assert(run_native_remove_command_with_source({repository}, root, source) == 0);
    assert(source.registryCalls + source.gitCalls > sourceCallsBeforeGitRemove);
    remaining = parse_native_manifest_file(root / "vix.toml");
    assert(remaining.valid() && remaining.value->packages.size() == 1);
    assert(!hasRegistryRoot(identity));
    assert(hasRegistryRoot(inferredIdentity));
    assert(!hasGitRoot(repository));
    assert(fs::exists(root / "vix.lock"));

    const int sourceCallsBeforeLastRemove = source.registryCalls + source.gitCalls;
    assert(run_native_remove_command_with_source({"acme/math"}, root, source) == 0);
    assert(source.registryCalls + source.gitCalls == sourceCallsBeforeLastRemove);
    remaining = parse_native_manifest_file(root / "vix.toml");
    assert(remaining.valid() && remaining.value->packages.empty());
    assert(!fs::exists(root / "vix.lock"));
    fs::remove_all(root);
  }
}

int main()
{
  command_parser_tests();
  runtime_configuration_tests();
  command_flow_tests();
}
