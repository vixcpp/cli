#include <vix/cli/project/native/Lockfile.hpp>

#include <toml.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <vix/cli/project/native/CanonicalHash.hpp>

namespace vix::cli::project::native
{
  namespace
  {
    bool ascii_less(std::string_view left, std::string_view right) noexcept
    {
      const auto common = std::min(left.size(), right.size());
      for (std::size_t index = 0; index < common; ++index)
      {
        const auto lhs = static_cast<unsigned char>(left[index]);
        const auto rhs = static_cast<unsigned char>(right[index]);
        if (lhs != rhs) return lhs < rhs;
      }
      return left.size() < right.size();
    }

    std::string version_text(const Version &version)
    {
      std::string result = std::to_string(version.major) + "." +
        std::to_string(version.minor) + "." + std::to_string(version.patch);
      if (version.prerelease)
      {
        result.push_back('-');
        result += *version.prerelease;
      }
      return result;
    }

    std::string quote_toml_string(const std::string &value)
    {
      std::ostringstream stream;
      const toml::value<std::string> tomlValue{value};
      stream << toml::toml_formatter{tomlValue, toml::format_flags::none};
      return stream.str();
    }

    void append_string_field(std::string &output, std::string_view name, const std::string &value)
    {
      output.append(name);
      output += " = ";
      output += quote_toml_string(value);
      output.push_back('\n');
    }

    std::string declared_value(const NormalizedDeclaredRegistry &declared)
    {
      std::string output = "{ source = \"registry\", registry = " + quote_toml_string(declared.identity.registry) +
        ", publisher = " + quote_toml_string(declared.identity.publisher) +
        ", name = " + quote_toml_string(declared.identity.name);
      std::visit([&](const auto &requirement)
      {
        using Requirement = std::decay_t<decltype(requirement)>;
        if constexpr (std::is_same_v<Requirement, ExactVersion>)
          output += ", exact = " + quote_toml_string(version_text(requirement.version));
        else
          output += ", compatible = " + quote_toml_string(version_text(requirement.version));
      }, declared.requirement);
      output += " }";
      return output;
    }

    template <typename Revision>
    std::pair<std::string_view, const std::string &> revision_mode(const Revision &revision)
    {
      return std::visit([](const auto &value) -> std::pair<std::string_view, const std::string &>
      {
        using Type = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Type, CommitRevision>) return {"commit", value.value};
        else if constexpr (std::is_same_v<Type, TagRevision>) return {"tag", value.value};
        else return {"branch", value.value};
      }, revision);
    }

    std::string declared_value(const NormalizedDeclaredGit &declared)
    {
      const auto [mode, revision] = revision_mode(declared.revision);
      return "{ source = \"git\", repository = " + quote_toml_string(declared.normalizedRepository) +
        ", " + std::string{mode} + " = " + quote_toml_string(revision) + " }";
    }

    std::string declared_value(const LockRoot &root)
    {
      return std::visit([](const auto &declared) { return declared_value(declared); }, root.declared);
    }

    LockfileSerializationIssue semantic_issue(const ValidationIssue &issue)
    {
      return {LockfileSerializationIssueKind::semantic_validation, issue.subject, issue.message,
              issue.code, std::nullopt, {}, {}};
    }

    LockfileSerializationIssue integrity_issue(const LockfileIntegrityIssue &issue)
    {
      return {LockfileSerializationIssueKind::integrity, issue.subject, issue.message,
              std::nullopt, issue.code, issue.storedValue, issue.expectedValue};
    }

    std::vector<RootRequirementDigestEntry> root_digest_entries(const Lockfile &lockfile)
    {
      std::vector<RootRequirementDigestEntry> entries;
      entries.reserve(lockfile.roots.size());
      for (const auto &root : lockfile.roots)
      {
        entries.push_back(RootRequirementDigestEntry{
          root.alias,
          std::visit([](const auto &declared) -> std::variant<NormalizedDeclaredRegistry, NormalizedRootGitRequirement>
          {
            using Declared = std::decay_t<decltype(declared)>;
            if constexpr (std::is_same_v<Declared, NormalizedDeclaredRegistry>)
              return declared;
            else
              return NormalizedRootGitRequirement{declared.normalizedRepository, declared.revision};
          }, root.declared)});
      }
      return entries;
    }

    void append_roots(std::string &output, const Lockfile &lockfile)
    {
      std::vector<const LockRoot *> roots;
      roots.reserve(lockfile.roots.size());
      for (const auto &root : lockfile.roots) roots.push_back(&root);
      std::sort(roots.begin(), roots.end(), [](const auto *left, const auto *right)
      { return ascii_less(left->alias, right->alias); });

      for (const auto *root : roots)
      {
        output += "\n[[roots]]\n";
        append_string_field(output, "alias", root->alias);
        output += "declared = ";
        output += declared_value(*root);
        output.push_back('\n');
        append_string_field(output, "node", root->node);
      }
    }

    std::string node_id(const LockNode &node)
    {
      return std::visit([](const auto &value) { return value.id; }, node);
    }

    void append_dependencies(std::string &output, const std::vector<LockDependency> &dependencies)
    {
      std::vector<const LockDependency *> ordered;
      ordered.reserve(dependencies.size());
      for (const auto &dependency : dependencies) ordered.push_back(&dependency);
      std::sort(ordered.begin(), ordered.end(), [](const auto *left, const auto *right)
      { return ascii_less(left->alias, right->alias); });
      for (const auto *dependency : ordered)
      {
        output += "\n[[nodes.dependencies]]\n";
        append_string_field(output, "alias", dependency->alias);
        append_string_field(output, "node", dependency->node);
      }
    }

    void append_node(std::string &output, const LockNode &lockNode)
    {
      output += "\n[[nodes]]\n";
      std::visit([&](const auto &node)
      {
        using Node = std::decay_t<decltype(node)>;
        append_string_field(output, "id", node.id);
        if constexpr (std::is_same_v<Node, RegistryLockNode>)
        {
          append_string_field(output, "source", "registry");
          append_string_field(output, "registry", node.identity.registry);
          append_string_field(output, "publisher", node.identity.publisher);
          append_string_field(output, "name", node.identity.name);
          append_string_field(output, "version", version_text(node.version));
        }
        else
        {
          append_string_field(output, "source", "git");
          append_string_field(output, "repository", node.normalizedRepository);
          append_string_field(output, "commit", node.commit);
        }
        append_string_field(output, "digest", node.contentDigest);
        append_dependencies(output, node.dependencies);
      }, lockNode);
    }
  } // namespace

  LockfileIntegrityResult validate_lockfile_integrity(const Lockfile &lockfile)
  {
    LockfileIntegrityResult result;
    const auto expectedRootDigest = root_requirements_digest(root_digest_entries(lockfile));
    if (lockfile.rootRequirementsDigest != expectedRootDigest)
      result.issues.push_back({LockfileIntegrityCode::root_requirements_digest_mismatch,
        "root-requirements-digest", "stored root requirement digest does not match declared root requirements",
        lockfile.rootRequirementsDigest, expectedRootDigest});

    for (std::size_t index = 0; index < lockfile.nodes.size(); ++index)
    {
      const auto &node = lockfile.nodes[index];
      std::visit([&](const auto &value)
      {
        using Node = std::decay_t<decltype(value)>;
        std::string expected;
        if constexpr (std::is_same_v<Node, RegistryLockNode>)
          expected = registry_node_id(RegistryNodeIdentity{value.identity, value.version});
        else
          expected = git_node_id(GitNodeIdentity{value.normalizedRepository, value.commit});
        if (value.id != expected)
          result.issues.push_back({LockfileIntegrityCode::node_id_mismatch,
            "nodes[" + std::to_string(index) + "].id", "stored node ID does not match immutable package identity",
            value.id, std::move(expected)});
      }, node);
    }
    return result;
  }

  LockfileSerializationResult serialize_lockfile(const Lockfile &lockfile)
  {
    LockfileSerializationResult result;
    const auto semantic = validate_lockfile(lockfile);
    if (!semantic.valid())
    {
      for (const auto &issue : semantic.issues) result.issues.push_back(semantic_issue(issue));
      return result;
    }

    const auto integrity = validate_lockfile_integrity(lockfile);
    if (!integrity.valid())
    {
      for (const auto &issue : integrity.issues) result.issues.push_back(integrity_issue(issue));
      return result;
    }

    std::string output = "format = 1\n";
    append_string_field(output, "root-requirements-digest", lockfile.rootRequirementsDigest);
    append_roots(output, lockfile);

    std::vector<const LockNode *> nodes;
    nodes.reserve(lockfile.nodes.size());
    for (const auto &node : lockfile.nodes) nodes.push_back(&node);
    std::sort(nodes.begin(), nodes.end(), [](const auto *left, const auto *right)
    { return ascii_less(node_id(*left), node_id(*right)); });
    for (const auto *node : nodes) append_node(output, *node);

    result.text = std::move(output);
    return result;
  }
} // namespace vix::cli::project::native
