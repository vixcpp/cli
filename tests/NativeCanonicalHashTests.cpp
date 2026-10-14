#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/TomlParser.hpp>

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
  RegistryNodeIdentity registry_node(Version version = Version{11, 1, 2, std::nullopt})
  {
    return RegistryNodeIdentity{{"vix", "fmtlib", "fmt"}, std::move(version)};
  }

  GitNodeIdentity git_node(std::string commit = "0123456789abcdef0123456789abcdef01234567")
  {
    return GitNodeIdentity{"https://example.invalid/org/protocol.git", std::move(commit)};
  }

  RootRequirementDigestEntry registry_root(
      std::string alias = "fmt",
      VersionRequirement requirement = CompatibleVersion{Version{11, 0, 0, std::nullopt}})
  {
    return RootRequirementDigestEntry{
      std::move(alias),
      NormalizedDeclaredRegistry{{"vix", "fmtlib", "fmt"}, std::move(requirement)}};
  }

  RootRequirementDigestEntry git_root(std::string alias = "protocol")
  {
    return RootRequirementDigestEntry{
      std::move(alias),
      NormalizedRootGitRequirement{
        "https://example.invalid/org/protocol.git",
        TagRevision{"v1.2.0"}}};
  }

  void sha256_tests()
  {
    assert(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(sha256_digest("abc") == "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  }

  void node_encoding_tests()
  {
    const std::string expectedRegistry =
      "VIX-LOCK-NODE-v1\n"
      "source=8:registry\n"
      "registry=3:vix\n"
      "publisher=6:fmtlib\n"
      "name=3:fmt\n"
      "version=6:11.1.2\n";
    const auto registryBytes = canonical_registry_node_bytes(registry_node());
    assert(registryBytes == expectedRegistry);
    assert(registryBytes.back() == '\n');
    assert(registryBytes.find('\r') == std::string::npos);
    assert(registryBytes.size() < 2 || registryBytes.substr(registryBytes.size() - 2) != "\n\n");
    assert(registry_node_id(registry_node()) == "sha256:" + sha256_hex(expectedRegistry));
    assert(registry_node_id(registry_node(Version{11, 1, 3, std::nullopt})) != registry_node_id(registry_node()));
    assert(canonical_registry_node_bytes(registry_node(Version{1, 0, 0, std::string{"beta.2"}})) ==
      "VIX-LOCK-NODE-v1\nsource=8:registry\nregistry=3:vix\npublisher=6:fmtlib\nname=3:fmt\nversion=12:1.0.0-beta.2\n");

    const std::string expectedGit =
      "VIX-LOCK-NODE-v1\n"
      "source=3:git\n"
      "repository=40:https://example.invalid/org/protocol.git\n"
      "package-root=1:.\n"
      "commit=40:0123456789abcdef0123456789abcdef01234567\n";
    const auto gitBytes = canonical_git_node_bytes(git_node());
    assert(gitBytes == expectedGit);
    assert(gitBytes.back() == '\n');
    assert(gitBytes.find('\r') == std::string::npos);
    assert(gitBytes.size() < 2 || gitBytes.substr(gitBytes.size() - 2) != "\n\n");
    assert(git_node_id(git_node("1123456789abcdef0123456789abcdef01234567")) != git_node_id(git_node()));

    // Structural identity APIs intentionally accept no lock ID, content digest,
    // or dependency list; these lock-only values cannot affect the node ID.
    const RegistryLockNode lockA{"stored-a", {"vix", "fmtlib", "fmt"}, Version{11, 1, 2, std::nullopt},
      "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", {}};
    const RegistryLockNode lockB{"stored-b", {"vix", "fmtlib", "fmt"}, Version{11, 1, 2, std::nullopt},
      "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", {{"tls", "other-node"}}};
    const RegistryNodeIdentity identityA{lockA.identity, lockA.version};
    const RegistryNodeIdentity identityB{lockB.identity, lockB.version};
    assert(registry_node_id(identityA) == registry_node_id(identityB));
  }

  void root_encoding_tests()
  {
    const std::string expectedRegistry =
      "VIX-ROOT-REQUIREMENTS-v1\n"
      "count=1\n"
      "entry\n"
      "alias=3:fmt\n"
      "source=8:registry\n"
      "registry=3:vix\n"
      "publisher=6:fmtlib\n"
      "name=3:fmt\n"
      "mode=10:compatible\n"
      "version=6:11.0.0\n";
    const std::vector<RootRequirementDigestEntry> oneRegistry{registry_root()};
    const auto registryBytes = canonical_root_requirements_bytes(oneRegistry);
    assert(registryBytes == expectedRegistry);
    assert(root_requirements_digest(oneRegistry) == "sha256:" + sha256_hex(expectedRegistry));

    const std::string expectedGit =
      "VIX-ROOT-REQUIREMENTS-v1\n"
      "count=1\n"
      "entry\n"
      "alias=8:protocol\n"
      "source=3:git\n"
      "repository=40:https://example.invalid/org/protocol.git\n"
      "package-root=1:.\n"
      "mode=3:tag\n"
      "revision=6:v1.2.0\n";
    const std::vector<RootRequirementDigestEntry> oneGit{git_root()};
    const auto gitBytes = canonical_root_requirements_bytes(oneGit);
    assert(gitBytes == expectedGit);
    assert(gitBytes.back() == '\n');
    assert(gitBytes.find('\r') == std::string::npos);
    assert(gitBytes.size() < 2 || gitBytes.substr(gitBytes.size() - 2) != "\n\n");

    const std::vector<RootRequirementDigestEntry> declarationOrderA{git_root(), registry_root()};
    const std::vector<RootRequirementDigestEntry> declarationOrderB{registry_root(), git_root()};
    assert(canonical_root_requirements_bytes(declarationOrderA) == canonical_root_requirements_bytes(declarationOrderB));
    assert(root_requirements_digest(declarationOrderA) == root_requirements_digest(declarationOrderB));

    assert(root_requirements_digest({registry_root("other")}) != root_requirements_digest(oneRegistry));
    assert(root_requirements_digest({RootRequirementDigestEntry{
      "fmt", NormalizedDeclaredRegistry{{"vix", "other-publisher", "fmt"}, CompatibleVersion{Version{11, 0, 0, std::nullopt}}}}}) !=
      root_requirements_digest(oneRegistry));
    assert(root_requirements_digest({registry_root("fmt", ExactVersion{Version{11, 0, 0, std::nullopt}})}) !=
      root_requirements_digest(oneRegistry));
    assert(root_requirements_digest({registry_root("fmt", CompatibleVersion{Version{11, 0, 1, std::nullopt}})}) !=
      root_requirements_digest(oneRegistry));
    assert(root_requirements_digest({git_root("protocol")}) != root_requirements_digest({RootRequirementDigestEntry{
      "protocol", NormalizedRootGitRequirement{"https://example.invalid/org/protocol.git", TagRevision{"v1.2.1"}}}}));
    assert(root_requirements_digest({registry_root("fmt", CompatibleVersion{Version{11, 0, 0, std::nullopt}}), git_root("other")}) !=
      root_requirements_digest({registry_root(), git_root()}));

    const std::string emptyExpected = "VIX-ROOT-REQUIREMENTS-v1\ncount=0\n";
    assert(canonical_root_requirements_bytes({}) == emptyExpected);
  }

  void utf8_byte_length_test()
  {
    const GitNodeIdentity identity{"https://example.invalid/\xC3\xA9", "0123456789abcdef0123456789abcdef01234567"};
    const auto bytes = canonical_git_node_bytes(identity);
    assert(bytes.find("repository=26:https://example.invalid/\xC3\xA9\n") != std::string::npos);
  }

  std::vector<RootRequirementDigestEntry> digest_entries(const NativeProject &project)
  {
    std::vector<RootRequirementDigestEntry> result;
    for (const auto &package : project.packages)
    {
      const auto &registry = std::get<RegistryRequirement>(package.source);
      result.push_back(RootRequirementDigestEntry{
        package.alias,
        NormalizedDeclaredRegistry{registry.identity, registry.version}});
    }
    return result;
  }

  void toml_representation_independence_test()
  {
    const auto compact = parse_native_manifest_text(R"toml(format=1
[project]
name="demo"
[[packages]]
alias="fmt"
source="registry"
registry="vix"
publisher="fmtlib"
name="fmt"
requirement={compatible="11.0.0"}
[[targets]]
name="app"
kind="executable"
sources=["main.cpp"]
)toml");
    const auto decorated = parse_native_manifest_text(R"toml(
# Semantic equivalent with different whitespace, comments, and table layout.
format = 1

[project]
name = 'demo' # root project

[[packages]]
alias = 'fmt'
source = 'registry'
registry = 'vix'
publisher = 'fmtlib'
name = 'fmt'
requirement = { compatible = '11.0.0' }

[[targets]]
name = 'app'
kind = 'executable'
sources = [ 'main.cpp' ]
)toml");
    assert(compact.valid() && decorated.valid());
    const auto compactEntries = digest_entries(*compact.value);
    const auto decoratedEntries = digest_entries(*decorated.value);
    assert(canonical_root_requirements_bytes(compactEntries) == canonical_root_requirements_bytes(decoratedEntries));
    assert(root_requirements_digest(compactEntries) == root_requirements_digest(decoratedEntries));
  }
}

int main()
{
  sha256_tests();
  node_encoding_tests();
  root_encoding_tests();
  utf8_byte_length_test();
  toml_representation_independence_test();
}
