/** @file Lockfile.hpp
 *  Structural lockfile integrity and deterministic format-1 text serialization.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_LOCKFILE_HPP
#define VIX_CLI_PROJECT_NATIVE_LOCKFILE_HPP

#include <optional>
#include <string>
#include <vector>

#include <vix/cli/project/native/Models.hpp>
#include <vix/cli/project/native/Validation.hpp>

namespace vix::cli::project::native
{
  enum class LockfileIntegrityCode
  {
    root_requirements_digest_mismatch,
    node_id_mismatch
  };

  struct LockfileIntegrityIssue
  {
    LockfileIntegrityCode code{LockfileIntegrityCode::node_id_mismatch};
    std::string subject;
    std::string message;
    std::string storedValue;
    std::string expectedValue;
  };

  struct LockfileIntegrityResult
  {
    std::vector<LockfileIntegrityIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return issues.empty(); }
  };

  [[nodiscard]] LockfileIntegrityResult validate_lockfile_integrity(const Lockfile &lockfile);

  enum class LockfileSerializationIssueKind
  {
    semantic_validation,
    integrity
  };

  struct LockfileSerializationIssue
  {
    LockfileSerializationIssueKind kind{LockfileSerializationIssueKind::semantic_validation};
    std::string subject;
    std::string message;
    std::optional<ValidationCode> validationCode;
    std::optional<LockfileIntegrityCode> integrityCode;
    std::string storedValue;
    std::string expectedValue;
  };

  struct LockfileSerializationResult
  {
    std::optional<std::string> text;
    std::vector<LockfileSerializationIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return text.has_value() && issues.empty(); }
  };

  // Serialization validates semantics and structural integrity. It never
  // repairs stored IDs/digests or writes files.
  [[nodiscard]] LockfileSerializationResult serialize_lockfile(const Lockfile &lockfile);
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_LOCKFILE_HPP
