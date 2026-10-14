#include <vix/cli/project/native/ProjectLockState.hpp>

#include <type_traits>
#include <utility>
#include <variant>

#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/Validation.hpp>

namespace vix::cli::project::native
{
  namespace
  {
    std::vector<RootRequirementDigestEntry> requirement_entries(const NativeProject &project)
    {
      std::vector<RootRequirementDigestEntry> entries;
      entries.reserve(project.packages.size());
      for (const auto &package : project.packages)
      {
        entries.push_back(RootRequirementDigestEntry{
          package.alias,
          std::visit([](const auto &source) -> std::variant<NormalizedDeclaredRegistry, NormalizedRootGitRequirement>
          {
            using Source = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<Source, RegistryRequirement>)
              return NormalizedDeclaredRegistry{source.identity, source.version};
            else
              return NormalizedRootGitRequirement{source.normalizedRepository, source.revision};
          }, package.source)});
      }
      return entries;
    }

    ProjectLockIssue semantic_issue(const ValidationIssue &issue)
    {
      return {ProjectLockIssueSource::lock_semantics, issue.subject, issue.message,
              issue.code, std::nullopt, {}, {}};
    }

    ProjectLockIssue integrity_issue(const LockfileIntegrityIssue &issue)
    {
      return {ProjectLockIssueSource::lock_integrity, issue.subject, issue.message,
              std::nullopt, issue.code, issue.storedValue, issue.expectedValue};
    }
  } // namespace

  std::string project_requirements_digest(const NativeProject &project)
  {
    return root_requirements_digest(requirement_entries(project));
  }

  ProjectLockStatus evaluate_project_lock_state(
      const NativeProject &project, const std::optional<Lockfile> &lockfile)
  {
    ProjectLockStatus status;
    const bool hasRequirements = !project.packages.empty();
    if (hasRequirements)
      status.expectedRequirementsDigest = project_requirements_digest(project);

    if (!lockfile)
    {
      status.state = hasRequirements ? ProjectLockState::missing : ProjectLockState::not_required;
      return status;
    }

    status.storedRequirementsDigest = lockfile->rootRequirementsDigest;
    const auto semantic = validate_lockfile(*lockfile);
    const auto integrity = validate_lockfile_integrity(*lockfile);
    if (!semantic.valid() || !integrity.valid())
    {
      status.state = ProjectLockState::invalid;
      for (const auto &issue : semantic.issues) status.issues.push_back(semantic_issue(issue));
      for (const auto &issue : integrity.issues) status.issues.push_back(integrity_issue(issue));
      return status;
    }

    if (!hasRequirements)
    {
      status.state = ProjectLockState::unnecessary;
      return status;
    }

    status.state = *status.expectedRequirementsDigest == lockfile->rootRequirementsDigest
      ? ProjectLockState::current
      : ProjectLockState::stale;
    return status;
  }
} // namespace vix::cli::project::native
