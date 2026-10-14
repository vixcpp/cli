#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/ProjectLockState.hpp>
#include <vix/cli/project/native/Validation.hpp>

#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace vix::cli::project::native;

namespace
{
  constexpr const char *content_digest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  constexpr const char *commit = "0123456789abcdef0123456789abcdef01234567";

  NativeProject project_with_target()
  {
    NativeProject project;
    project.name = "consumer";
    NativeTarget app;
    app.name = "app";
    app.kind = TargetKind::executable;
    app.sources = {"main.cpp"};
    project.targets.push_back(std::move(app));
    return project;
  }

  PackageRequirement registry_requirement(
      std::string alias = "fmt",
      std::string name = "fmt",
      VersionRequirement requirement = CompatibleVersion{Version{11, 0, 0, std::nullopt}})
  {
    return {std::move(alias), RegistryRequirement{{"vix", "fmtlib", std::move(name)}, std::move(requirement)}};
  }

  PackageRequirement git_requirement(
      std::string alias = "protocol",
      std::string repository = "https://example.invalid/protocol",
      GitRevisionRequirement revision = TagRevision{"v1.2.0"})
  {
    return {std::move(alias), GitRequirement{std::move(repository), true, std::move(revision)}};
  }

  std::vector<RootRequirementDigestEntry> digest_entries(const NativeProject &project)
  {
    std::vector<RootRequirementDigestEntry> entries;
    for (const auto &requirement : project.packages)
    {
      entries.push_back({requirement.alias, std::visit([](const auto &source)
      -> std::variant<NormalizedDeclaredRegistry, NormalizedRootGitRequirement>
      {
        using Source = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<Source, RegistryRequirement>)
          return NormalizedDeclaredRegistry{source.identity, source.version};
        else
          return NormalizedRootGitRequirement{source.normalizedRepository, source.revision};
      }, requirement.source)});
    }
    return entries;
  }

  Lockfile lock_for(const NativeProject &project)
  {
    Lockfile lock;
    lock.rootRequirementsDigest = project_requirements_digest(project);
    for (const auto &requirement : project.packages)
    {
      if (const auto *registry = std::get_if<RegistryRequirement>(&requirement.source))
      {
        const Version version = std::visit([](const auto &mode) { return mode.version; }, registry->version);
        RegistryLockNode node;
        node.identity = registry->identity;
        node.version = version;
        node.id = registry_node_id(RegistryNodeIdentity{node.identity, node.version});
        node.contentDigest = content_digest;
        lock.roots.push_back({requirement.alias,
          NormalizedDeclaredRegistry{registry->identity, registry->version}, node.id});
        lock.nodes.emplace_back(std::move(node));
      }
      else
      {
        const auto &git = std::get<GitRequirement>(requirement.source);
        const std::string resolvedCommit = std::visit([](const auto &revision) -> std::string
        {
          using Revision = std::decay_t<decltype(revision)>;
          if constexpr (std::is_same_v<Revision, CommitRevision>) return revision.value;
          else return commit;
        }, git.revision);
        GitLockNode node;
        node.normalizedRepository = git.normalizedRepository;
        node.commit = resolvedCommit;
        node.id = git_node_id(GitNodeIdentity{node.normalizedRepository, node.commit});
        node.contentDigest = content_digest;
        lock.roots.push_back({requirement.alias,
          NormalizedDeclaredGit{git.normalizedRepository, git.repositoryRoot, git.revision}, node.id});
        lock.nodes.emplace_back(std::move(node));
      }
    }
    return lock;
  }

  ProjectLockStatus evaluate(const NativeProject &project, const Lockfile &lock)
  {
    return evaluate_project_lock_state(project, std::optional<Lockfile>{lock});
  }

  void basic_state_tests()
  {
    const auto emptyProject = project_with_target();
    assert(validate_native_project(emptyProject).valid());
    assert(evaluate_project_lock_state(emptyProject, std::nullopt).state == ProjectLockState::not_required);

    auto project = project_with_target();
    project.packages.push_back(registry_requirement());
    assert(validate_native_project(project).valid());
    assert(evaluate_project_lock_state(project, std::nullopt).state == ProjectLockState::missing);

    const auto lock = lock_for(project);
    assert(validate_lockfile(lock).valid());
    assert(validate_lockfile_integrity(lock).valid());
    const auto current = evaluate(project, lock);
    assert(current.state == ProjectLockState::current);
    assert(current.issues.empty());
    assert(current.expectedRequirementsDigest == lock.rootRequirementsDigest);
    assert(current.storedRequirementsDigest == lock.rootRequirementsDigest);

    assert(evaluate(emptyProject, lock).state == ProjectLockState::unnecessary);

    auto invalidLock = lock;
    auto &node = std::get<RegistryLockNode>(invalidLock.nodes.front());
    node.id = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    invalidLock.roots.front().node = node.id;
    const auto unnecessaryInvalid = evaluate(emptyProject, invalidLock);
    assert(unnecessaryInvalid.state == ProjectLockState::invalid);
    assert(!unnecessaryInvalid.issues.empty());
  }

  void stale_and_order_tests()
  {
    auto project = project_with_target();
    project.packages = {registry_requirement("fmt"), git_requirement()};
    assert(validate_native_project(project).valid());
    const auto lock = lock_for(project);
    assert(evaluate(project, lock).state == ProjectLockState::current);

    auto reordered = project;
    std::reverse(reordered.packages.begin(), reordered.packages.end());
    assert(project_requirements_digest(reordered) == project_requirements_digest(project));
    assert(evaluate(reordered, lock).state == ProjectLockState::current);

    auto aliasChanged = project;
    aliasChanged.packages[0].alias = "fmtlib";
    assert(evaluate(aliasChanged, lock).state == ProjectLockState::stale);

    auto registryChanged = project;
    registryChanged.packages[0] = registry_requirement(
      "fmt", "fmt", ExactVersion{Version{11, 2, 0, std::nullopt}});
    assert(evaluate(registryChanged, lock).state == ProjectLockState::stale);

    auto gitChanged = project;
    gitChanged.packages[1] = git_requirement(
      "protocol", "https://example.invalid/other", BranchRevision{"main"});
    assert(evaluate(gitChanged, lock).state == ProjectLockState::stale);

    auto unusedProject = project_with_target();
    unusedProject.packages.push_back(registry_requirement());
    const auto unusedLock = lock_for(unusedProject);
    auto changedUnused = unusedProject;
    changedUnused.packages.front() = registry_requirement(
      "fmt", "fmt", CompatibleVersion{Version{12, 0, 0, std::nullopt}});
    assert(changedUnused.targets.front().packageUses.empty());
    assert(evaluate(changedUnused, unusedLock).state == ProjectLockState::stale);
  }

  void target_usage_and_root_mapping_tests()
  {
    auto project = project_with_target();
    project.packages.push_back(registry_requirement());
    auto lock = lock_for(project);
    project.targets.front().packageUses.push_back({UsageVisibility::private_, "fmt", "fmt"});
    assert(validate_native_project(project).valid());
    assert(evaluate(project, lock).state == ProjectLockState::current);

    project.targets.front().packageUses.front().exportName = "format-api";
    assert(validate_native_project(project).valid());
    assert(evaluate(project, lock).state == ProjectLockState::current);

    auto remappedNode = lock;
    remappedNode.roots.front().node = "sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    const auto status = evaluate(project, remappedNode);
    assert(status.state == ProjectLockState::invalid);
    assert(status.expectedRequirementsDigest == project_requirements_digest(project));
    assert(status.expectedRequirementsDigest == lock.rootRequirementsDigest);
  }

  void invalid_precedence_tests()
  {
    auto project = project_with_target();
    project.packages.push_back(registry_requirement());
    auto lock = lock_for(project);

    auto badNode = lock;
    std::get<RegistryLockNode>(badNode.nodes.front()).id =
      "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    badNode.roots.front().node = std::get<RegistryLockNode>(badNode.nodes.front()).id;
    assert(evaluate(project, badNode).state == ProjectLockState::invalid);

    auto badDigest = lock;
    badDigest.rootRequirementsDigest =
      "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
    assert(evaluate(project, badDigest).state == ProjectLockState::invalid);

    auto changedProject = project;
    changedProject.packages.front().alias = "different";
    assert(project_requirements_digest(changedProject) != lock.rootRequirementsDigest);
    const auto invalidAndWouldBeStale = evaluate(changedProject, badDigest);
    assert(invalidAndWouldBeStale.state == ProjectLockState::invalid);
    assert(!invalidAndWouldBeStale.issues.empty());
  }

  void root_requirement_digest_scope_test()
  {
    auto project = project_with_target();
    project.packages.push_back(registry_requirement());
    auto lock = lock_for(project);
    const auto originalDigest = lock.rootRequirementsDigest;
    lock.roots.front().node = "arbitrary-resolution-reference";
    assert(root_requirements_digest(digest_entries(project)) == originalDigest);
    assert(project_requirements_digest(project) == originalDigest);
  }
}

int main()
{
  basic_state_tests();
  stale_and_order_tests();
  target_usage_and_root_mapping_tests();
  invalid_precedence_tests();
  root_requirement_digest_scope_test();
}
