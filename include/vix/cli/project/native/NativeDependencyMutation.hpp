/** @file NativeDependencyMutation.hpp
 *  Native dependency-command orchestration and coordinated project-state writes.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_NATIVE_DEPENDENCY_MUTATION_HPP
#define VIX_CLI_PROJECT_NATIVE_NATIVE_DEPENDENCY_MUTATION_HPP

#include <filesystem>
#include <system_error>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/project/native/MetadataAcquisition.hpp>
#include <vix/cli/project/native/TomlParser.hpp>

namespace vix::cli::project::native
{
  enum class ProjectStateTransactionCode
  {
    invalid_candidate,
    read_failure,
    temporary_write_failure,
    backup_failure,
    replacement_failure,
    rollback_failure
  };

  struct ProjectStateTransactionIssue
  {
    ProjectStateTransactionCode code{ProjectStateTransactionCode::replacement_failure};
    std::string path;
    std::string message;
  };

  struct ProjectStateTransactionResult
  {
    bool committed{false};
    std::vector<ProjectStateTransactionIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return committed && issues.empty(); }
  };

  class ProjectStateFileOperations
  {
  public:
    virtual ~ProjectStateFileOperations() = default;
    [[nodiscard]] virtual bool is_directory(const std::filesystem::path &path,
      std::error_code &error) const = 0;
    [[nodiscard]] virtual bool exists(const std::filesystem::path &path,
      std::error_code &error) const = 0;
    [[nodiscard]] virtual bool write_complete(const std::filesystem::path &path,
      std::string_view bytes) = 0;
    virtual void rename(const std::filesystem::path &from, const std::filesystem::path &to,
      std::error_code &error) = 0;
    virtual void remove(const std::filesystem::path &path, std::error_code &error) = 0;
  };

  // Candidate bytes are fully serialized and validated before this class is
  // called. A missing lockText means the successful candidate has no lock.
  class NativeProjectStateTransaction final
  {
  public:
    explicit NativeProjectStateTransaction(std::filesystem::path projectRoot,
      ProjectStateFileOperations *fileOperations = nullptr);
    [[nodiscard]] ProjectStateTransactionResult commit(
      bool writeManifest,
      const std::optional<std::string> &manifestText,
      const std::optional<std::string> &lockText);
  private:
    std::filesystem::path root_;
    ProjectStateFileOperations *fileOperations_;
  };

  enum class NativeDependencyMutationCode
  {
    project_not_found,
    invalid_project,
    invalid_requirement,
    requirement_already_exists,
    requirement_not_found,
    metadata_failure,
    resolution_failure,
    lock_failure,
    persistence_failure
  };

  struct NativeDependencyMutationIssue
  {
    NativeDependencyMutationCode code{NativeDependencyMutationCode::invalid_project};
    std::string subject;
    std::string message;
  };

  struct NativeDependencyMutationResult
  {
    bool succeeded{false};
    std::vector<NativeDependencyMutationIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return succeeded && issues.empty(); }
  };

  // Thin operation service: it mutates a candidate NativeProject in memory,
  // observes metadata and resolves the entire graph, then publishes state.
  // It never acquires package bytes or materializes packages.
  class NativeDependencyMutationService final
  {
  public:
    NativeDependencyMutationService(std::filesystem::path projectRoot,
      PackageMetadataSource &onlineMetadata,
      ProjectStateFileOperations *fileOperations = nullptr);
    [[nodiscard]] NativeDependencyMutationResult add(PackageRequirement requirement);
    [[nodiscard]] NativeDependencyMutationResult remove(std::string_view rootAlias);
    [[nodiscard]] NativeDependencyMutationResult remove_identity(const PackageIdentity &identity);
    [[nodiscard]] NativeDependencyMutationResult update();
  private:
    [[nodiscard]] NativeDependencyMutationResult resolve_and_commit(
      NativeProject candidate, bool writeManifest);
    [[nodiscard]] NativeDependencyMutationResult commit_empty(NativeProject candidate);
    std::filesystem::path root_;
    PackageMetadataSource &onlineMetadata_;
    ProjectStateFileOperations *fileOperations_;
  };
}

#endif
