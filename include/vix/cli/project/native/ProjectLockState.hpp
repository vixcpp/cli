/** @file ProjectLockState.hpp
 *  Pure classification of project package requirements against an optional lock.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_PROJECT_LOCK_STATE_HPP
#define VIX_CLI_PROJECT_NATIVE_PROJECT_LOCK_STATE_HPP

#include <optional>
#include <string>
#include <vector>

#include <vix/cli/project/native/Lockfile.hpp>
#include <vix/cli/project/native/Models.hpp>

namespace vix::cli::project::native
{
  enum class ProjectLockState
  {
    not_required,
    missing,
    current,
    stale,
    invalid,
    unnecessary
  };

  enum class ProjectLockIssueSource
  {
    lock_semantics,
    lock_integrity
  };

  struct ProjectLockIssue
  {
    ProjectLockIssueSource source{ProjectLockIssueSource::lock_semantics};
    std::string subject;
    std::string message;
    std::optional<ValidationCode> validationCode;
    std::optional<LockfileIntegrityCode> integrityCode;
    std::string storedValue;
    std::string expectedValue;
  };

  struct ProjectLockStatus
  {
    ProjectLockState state{ProjectLockState::not_required};
    std::optional<std::string> expectedRequirementsDigest;
    std::optional<std::string> storedRequirementsDigest;
    std::vector<ProjectLockIssue> issues;
  };

  // The project must already have passed validate_native_project(). This pure
  // operation performs no filesystem access, resolution, or lock mutation.
  [[nodiscard]] std::string project_requirements_digest(const NativeProject &project);
  [[nodiscard]] ProjectLockStatus evaluate_project_lock_state(
    const NativeProject &project, const std::optional<Lockfile> &lockfile);
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_PROJECT_LOCK_STATE_HPP
