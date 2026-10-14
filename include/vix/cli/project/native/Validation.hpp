/** @file Validation.hpp
 *  Pure and explicitly root-aware validation for native package models.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_VALIDATION_HPP
#define VIX_CLI_PROJECT_NATIVE_VALIDATION_HPP

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/project/native/Models.hpp>

namespace vix::cli::project::native
{
  enum class ValidationCode
  {
    unsupported_format,
    invalid_project_name,
    invalid_target_name,
    invalid_target_kind_semantics,
    duplicate_target,
    invalid_target_sources,
    duplicate_source,
    invalid_path,
    invalid_cxx_standard,
    invalid_visibility,
    invalid_compile_definition,
    unknown_target,
    self_dependency,
    target_cycle,
    invalid_test,
    duplicate_package_alias,
    invalid_package_alias,
    invalid_package_identity,
    invalid_requirement,
    unknown_package_alias,
    duplicate_package_use,
    invalid_package_contract,
    duplicate_export,
    invalid_include_root,
    invalid_export_mapping,
    unknown_lock_node,
    duplicate_lock_root,
    duplicate_lock_node,
    duplicate_dependency_alias,
    malformed_digest,
    lock_cycle,
    noncanonical_empty_lock
  };

  struct ValidationLocation
  {
    std::string source;
    std::optional<std::size_t> line;
    std::optional<std::size_t> column;
  };

  struct ValidationIssue
  {
    ValidationCode code{ValidationCode::invalid_project_name};
    std::string subject;
    std::string message;
    std::optional<ValidationLocation> location;
  };

  struct ValidationResult
  {
    std::vector<ValidationIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return issues.empty(); }
  };

  [[nodiscard]] std::string_view to_string(ValidationCode code) noexcept;
  [[nodiscard]] bool valid_package_alias(std::string_view value) noexcept;
  [[nodiscard]] bool valid_registry_id(std::string_view value) noexcept;
  [[nodiscard]] bool valid_registry_name(std::string_view value) noexcept;
  [[nodiscard]] bool valid_semver(const Version &version) noexcept;
  [[nodiscard]] bool valid_requirement(const VersionRequirement &requirement) noexcept;
  [[nodiscard]] bool valid_package_content_digest(std::string_view value) noexcept;
  [[nodiscard]] std::string normalize_project_relative_path(std::string_view path);

  [[nodiscard]] ValidationResult validate_native_project(const NativeProject &project);
  [[nodiscard]] ValidationResult validate_native_project_paths(
      const NativeProject &project,
      const std::filesystem::path &projectRoot,
      bool requireExistingSources = true);

  [[nodiscard]] ValidationResult validate_package_contract(const PackageContract &contract);
  [[nodiscard]] ValidationResult validate_package_contract_paths(
      const PackageContract &contract,
      const std::filesystem::path &packageRoot);
  [[nodiscard]] ValidationResult validate_native_package(
      const NativeProject &project,
      const PackageContract &contract,
      const std::filesystem::path &packageRoot);

  [[nodiscard]] ValidationResult validate_lockfile(const Lockfile &lockfile);
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_VALIDATION_HPP
