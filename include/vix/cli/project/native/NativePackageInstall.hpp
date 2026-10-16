/** @file NativePackageInstall.hpp
 *  Locked package content acquisition and materialization for native projects.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_NATIVE_PACKAGE_INSTALL_HPP
#define VIX_CLI_PROJECT_NATIVE_NATIVE_PACKAGE_INSTALL_HPP

#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/project/native/PackageContentAcquisition.hpp>
#include <vix/cli/project/native/PackageMaterialization.hpp>
#include <vix/cli/project/native/FilesystemPackageContentStore.hpp>
#include <vix/cli/project/native/ProjectLockState.hpp>

namespace vix::cli::project::native
{
  enum class NativePackageInstallIssueCode
  {
    project_not_found,
    invalid_project,
    lock_missing,
    lock_invalid,
    lock_stale,
    lock_unnecessary,
    content_acquisition_failed,
    materialization_failed,
    runtime_configuration_failed,
    offline_content_missing,
    corrupt_cached_content
  };

  struct NativePackageInstallIssue
  {
    NativePackageInstallIssueCode code{NativePackageInstallIssueCode::invalid_project};
    std::string subject;
    std::string message;
  };

  struct NativePackageInstallResult
  {
    std::vector<PackageContentAcquisitionEntry> content;
    std::vector<PackageMaterializationResult> materializations;
    std::vector<NativePackageInstallIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return issues.empty(); }
  };

  struct NativePackageInstallParseResult
  {
    bool valid{false};
    bool offline{false};
    std::string diagnostic;
  };

  [[nodiscard]] NativePackageInstallParseResult parse_native_install_arguments(
    const std::vector<std::string> &arguments);

  // Reads and validates the existing manifest/lock, ensures all unique locked
  // content is present before materializing any package. It never resolves or
  // writes project state.
  [[nodiscard]] NativePackageInstallResult install_native_project_packages(
    const std::filesystem::path &projectRoot,
    AcquisitionMode mode,
    const RegistryDirectory &registries,
    RegistryPackageContentSource &registrySource,
    GitPackageContentSource &gitSource,
    FilesystemPackageContentStore &store,
    const std::filesystem::path &materializationRoot);

  [[nodiscard]] std::filesystem::path package_content_store_path();
  [[nodiscard]] std::filesystem::path package_materialization_root(
    const std::filesystem::path &projectRoot);
  [[nodiscard]] int run_native_install_command(
    const std::vector<std::string> &arguments,
    const std::filesystem::path &workingDirectory = std::filesystem::current_path());
} // namespace vix::cli::project::native

#endif
