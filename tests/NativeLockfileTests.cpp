#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/Lockfile.hpp>
#include <vix/cli/project/native/TomlParser.hpp>

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
  constexpr std::string_view content_digest_a = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  constexpr std::string_view content_digest_b = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

  Lockfile registry_lock()
  {
    Lockfile lock;
    const RegistryPackageIdentity identity{"vix", "fmtlib", "fmt"};
    const Version resolved{11, 1, 2, std::nullopt};
    RegistryLockNode node;
    node.identity = identity;
    node.version = resolved;
    node.id = registry_node_id(RegistryNodeIdentity{identity, resolved});
    node.contentDigest = std::string{content_digest_a};

    NormalizedDeclaredRegistry declared{identity, CompatibleVersion{Version{11, 0, 0, std::nullopt}}};
    lock.roots.push_back({"fmt", declared, node.id});
    lock.rootRequirementsDigest = root_requirements_digest({RootRequirementDigestEntry{"fmt", declared}});
    lock.nodes.emplace_back(std::move(node));
    return lock;
  }

  Lockfile git_lock(std::string repository = "https://example.invalid/protocol",
                    std::string revision = "v1.2.0")
  {
    Lockfile lock;
    const std::string commit = "0123456789abcdef0123456789abcdef01234567";
    GitLockNode node;
    node.normalizedRepository = repository;
    node.commit = commit;
    node.id = git_node_id(GitNodeIdentity{repository, commit});
    node.contentDigest = std::string{content_digest_a};

    NormalizedDeclaredGit declared{repository, true, TagRevision{revision}};
    lock.roots.push_back({"protocol", declared, node.id});
    lock.rootRequirementsDigest = root_requirements_digest({RootRequirementDigestEntry{
      "protocol", NormalizedRootGitRequirement{repository, declared.revision}}});
    lock.nodes.emplace_back(std::move(node));
    return lock;
  }

  GitLockNode git_node(std::string repository, char commit_character)
  {
    GitLockNode node;
    node.normalizedRepository = std::move(repository);
    node.commit = std::string(40, commit_character);
    node.id = git_node_id(GitNodeIdentity{node.normalizedRepository, node.commit});
    node.contentDigest = std::string{content_digest_a};
    return node;
  }

  void set_root_digest(Lockfile &lock)
  {
    std::vector<RootRequirementDigestEntry> entries;
    for (const auto &root : lock.roots)
    {
      entries.push_back({root.alias, std::visit([](const auto &declared)
      -> std::variant<NormalizedDeclaredRegistry, NormalizedRootGitRequirement>
      {
        using Declared = std::decay_t<decltype(declared)>;
        if constexpr (std::is_same_v<Declared, NormalizedDeclaredRegistry>) return declared;
        else return NormalizedRootGitRequirement{declared.normalizedRepository, declared.revision};
      }, root.declared)});
    }
    lock.rootRequirementsDigest = root_requirements_digest(entries);
  }

  bool same_revision(const GitRevisionRequirement &left, const GitRevisionRequirement &right)
  {
    if (left.index() != right.index()) return false;
    return std::visit([&](const auto &revision)
    {
      using Type = std::decay_t<decltype(revision)>;
      const auto *other = std::get_if<Type>(&right);
      return other && revision.value == other->value;
    }, left);
  }

  bool same_requirement(const VersionRequirement &left, const VersionRequirement &right)
  {
    if (left.index() != right.index()) return false;
    return std::visit([&](const auto &requirement)
    {
      using Type = std::decay_t<decltype(requirement)>;
      const auto *other = std::get_if<Type>(&right);
      return other && requirement.version == other->version;
    }, left);
  }

  bool same_lock(const Lockfile &left, const Lockfile &right)
  {
    if (left.format != right.format || left.rootRequirementsDigest != right.rootRequirementsDigest ||
        left.roots.size() != right.roots.size() || left.nodes.size() != right.nodes.size()) return false;
    for (std::size_t index = 0; index < left.roots.size(); ++index)
    {
      const auto &a = left.roots[index];
      const auto &b = right.roots[index];
      if (a.alias != b.alias || a.node != b.node || a.declared.index() != b.declared.index()) return false;
      const bool declaredEqual = std::visit([&](const auto &declared)
      {
        using Type = std::decay_t<decltype(declared)>;
        const auto *other = std::get_if<Type>(&b.declared);
        if (!other) return false;
        if constexpr (std::is_same_v<Type, NormalizedDeclaredRegistry>)
          return declared.identity == other->identity && same_requirement(declared.requirement, other->requirement);
        else
          return declared.normalizedRepository == other->normalizedRepository &&
            declared.repositoryRoot == other->repositoryRoot && same_revision(declared.revision, other->revision);
      }, a.declared);
      if (!declaredEqual) return false;
    }
    for (std::size_t index = 0; index < left.nodes.size(); ++index)
    {
      if (left.nodes[index].index() != right.nodes[index].index()) return false;
      const bool nodeEqual = std::visit([&](const auto &node)
      {
        using Type = std::decay_t<decltype(node)>;
        const auto *other = std::get_if<Type>(&right.nodes[index]);
        if (!other || node.id != other->id || node.contentDigest != other->contentDigest ||
            node.dependencies.size() != other->dependencies.size()) return false;
        if constexpr (std::is_same_v<Type, RegistryLockNode>)
        {
          if (!(node.identity == other->identity) || !(node.version == other->version)) return false;
        }
        else if (node.normalizedRepository != other->normalizedRepository ||
                 node.repositoryRoot != other->repositoryRoot || node.commit != other->commit) return false;
        for (std::size_t edge = 0; edge < node.dependencies.size(); ++edge)
          if (node.dependencies[edge].alias != other->dependencies[edge].alias ||
              node.dependencies[edge].node != other->dependencies[edge].node) return false;
        return true;
      }, left.nodes[index]);
      if (!nodeEqual) return false;
    }
    return true;
  }

  void integrity_tests()
  {
    auto registry = registry_lock();
    assert(validate_lockfile(registry).valid());
    assert(validate_lockfile_integrity(registry).valid());

    auto badRegistryId = registry;
    auto &badRegistryNode = std::get<RegistryLockNode>(badRegistryId.nodes.front());
    badRegistryNode.id = "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
    badRegistryId.roots.front().node = badRegistryNode.id;
    const auto registryIntegrity = validate_lockfile_integrity(badRegistryId);
    assert(!registryIntegrity.valid());
    assert(registryIntegrity.issues.front().code == LockfileIntegrityCode::node_id_mismatch);
    assert(registryIntegrity.issues.front().subject == "nodes[0].id");
    const auto rejectedRegistry = serialize_lockfile(badRegistryId);
    assert(!rejectedRegistry.valid() && !rejectedRegistry.text);
    assert(std::any_of(rejectedRegistry.issues.begin(), rejectedRegistry.issues.end(), [](const auto &issue)
    { return issue.integrityCode == LockfileIntegrityCode::node_id_mismatch; }));

    auto git = git_lock();
    assert(validate_lockfile(git).valid());
    assert(validate_lockfile_integrity(git).valid());
    auto badGitId = git;
    auto &badGitNode = std::get<GitLockNode>(badGitId.nodes.front());
    badGitNode.id = "sha256:eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    badGitId.roots.front().node = badGitNode.id;
    const auto gitIntegrity = validate_lockfile_integrity(badGitId);
    assert(!gitIntegrity.valid());
    assert(gitIntegrity.issues.front().code == LockfileIntegrityCode::node_id_mismatch);

    auto badRootDigest = registry;
    badRootDigest.rootRequirementsDigest = std::string{content_digest_b};
    const auto rootIntegrity = validate_lockfile_integrity(badRootDigest);
    assert(!rootIntegrity.valid());
    assert(rootIntegrity.issues.front().code == LockfileIntegrityCode::root_requirements_digest_mismatch);
    assert(rootIntegrity.issues.front().subject == "root-requirements-digest");
    const auto rejectedRoot = serialize_lockfile(badRootDigest);
    assert(!rejectedRoot.valid() && !rejectedRoot.text);
    assert(std::any_of(rejectedRoot.issues.begin(), rejectedRoot.issues.end(), [](const auto &issue)
    { return issue.integrityCode == LockfileIntegrityCode::root_requirements_digest_mismatch; }));

    auto changedResolution = registry;
    changedResolution.roots.front().node = "not-the-resolved-node";
    assert(validate_lockfile_integrity(changedResolution).valid());
  }

  void serialization_tests()
  {
    const auto lock = registry_lock();
    const auto serialized = serialize_lockfile(lock);
    assert(serialized.valid() && serialized.text);
    const auto &node = std::get<RegistryLockNode>(lock.nodes.front());
    const std::string expected =
      "format = 1\n"
      "root-requirements-digest = \"" + lock.rootRequirementsDigest + "\"\n"
      "\n[[roots]]\n"
      "alias = \"fmt\"\n"
      "declared = { source = \"registry\", registry = \"vix\", publisher = \"fmtlib\", name = \"fmt\", compatible = \"11.0.0\" }\n"
      "node = \"" + node.id + "\"\n"
      "\n[[nodes]]\n"
      "id = \"" + node.id + "\"\n"
      "source = \"registry\"\n"
      "registry = \"vix\"\n"
      "publisher = \"fmtlib\"\n"
      "name = \"fmt\"\n"
      "version = \"11.1.2\"\n"
      "digest = \"" + node.contentDigest + "\"\n";
    assert(*serialized.text == expected);
    assert(serialized.text->find("root-requirements-digest") < serialized.text->find("[[roots]]"));
    assert(serialized.text->find("dependencies") == std::string::npos);

    const auto parsed = parse_lockfile_text(*serialized.text, "serialized.lock");
    assert(parsed.valid());
    assert(same_lock(lock, *parsed.value));
    assert(validate_lockfile(*parsed.value).valid());
    assert(validate_lockfile_integrity(*parsed.value).valid());

    const auto git = git_lock("https://example.invalid/a\"b\\c", "v1.2.0-\"quoted\"\\tag");
    const auto gitSerialized = serialize_lockfile(git);
    assert(gitSerialized.valid() && gitSerialized.text);
    const auto gitParsed = parse_lockfile_text(*gitSerialized.text, "escaped.lock");
    assert(gitParsed.valid());
    assert(same_lock(git, *gitParsed.value));
    assert(gitSerialized.text->find("package-root") == std::string::npos);
    assert(gitSerialized.text->find("declared = { source = \"git\"") != std::string::npos);

    auto noncanonicalText = std::string{*serialized.text};
    noncanonicalText.insert(0, "# accepted comments and extra layout\n\n");
    const auto noncanonicalParsed = parse_lockfile_text(noncanonicalText, "input.lock");
    assert(noncanonicalParsed.valid());
    const auto canonicalized = serialize_lockfile(*noncanonicalParsed.value);
    assert(canonicalized.valid() && canonicalized.text);
    assert(*canonicalized.text == *serialized.text);
    const auto reparsedCanonical = parse_lockfile_text(*canonicalized.text);
    assert(reparsedCanonical.valid());
    assert(same_lock(*noncanonicalParsed.value, *reparsedCanonical.value));
  }

  Lockfile ordered_graph()
  {
    Lockfile lock;
    auto a = git_node("https://example.invalid/a", 'a');
    auto b = git_node("https://example.invalid/b", 'b');
    auto c = git_node("https://example.invalid/c", 'c');
    a.dependencies = {{"z", c.id}, {"a", b.id}};
    lock.roots = {
      {"zeta", NormalizedDeclaredGit{"https://example.invalid/a", true, BranchRevision{"main"}}, a.id},
      {"beta", NormalizedDeclaredGit{"https://example.invalid/b", true, TagRevision{"v1.0.0"}}, b.id}};
    lock.nodes = {a, b, c};
    set_root_digest(lock);
    return lock;
  }

  void deterministic_ordering_and_boundary_tests()
  {
    const auto original = ordered_graph();
    assert(validate_lockfile(original).valid());
    assert(validate_lockfile_integrity(original).valid());
    auto reordered = original;
    std::reverse(reordered.roots.begin(), reordered.roots.end());
    std::reverse(reordered.nodes.begin(), reordered.nodes.end());
    auto &a = std::get<GitLockNode>(*std::find_if(reordered.nodes.begin(), reordered.nodes.end(), [](const auto &node)
    { return std::get<GitLockNode>(node).normalizedRepository == "https://example.invalid/a"; }));
    std::reverse(a.dependencies.begin(), a.dependencies.end());
    const auto firstText = serialize_lockfile(original);
    const auto reorderedText = serialize_lockfile(reordered);
    assert(firstText.valid() && reorderedText.valid());
    assert(*firstText.text == *reorderedText.text);

    auto changedMetadata = original;
    auto &aNode = *std::find_if(changedMetadata.nodes.begin(), changedMetadata.nodes.end(), [](const auto &node)
    { return std::get<GitLockNode>(node).normalizedRepository == "https://example.invalid/a"; });
    auto &mutableA = std::get<GitLockNode>(aNode);
    const auto idBefore = git_node_id(GitNodeIdentity{mutableA.normalizedRepository, mutableA.commit});
    mutableA.contentDigest = std::string{content_digest_b};
    mutableA.dependencies.push_back({"extra", std::get<GitLockNode>(*std::find_if(changedMetadata.nodes.begin(), changedMetadata.nodes.end(), [](const auto &node)
    { return std::get<GitLockNode>(node).normalizedRepository == "https://example.invalid/c"; })).id});
    assert(git_node_id(GitNodeIdentity{mutableA.normalizedRepository, mutableA.commit}) == idBefore);
    assert(validate_lockfile_integrity(changedMetadata).valid());
    const auto changedText = serialize_lockfile(changedMetadata);
    assert(changedText.valid());
    assert(*changedText.text != *firstText.text);
  }
}

int main()
{
  integrity_tests();
  serialization_tests();
  deterministic_ordering_and_boundary_tests();
}
