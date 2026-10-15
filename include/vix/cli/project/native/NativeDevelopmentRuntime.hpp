/** @file NativeDevelopmentRuntime.hpp
 *  Native build/run/test orchestration over the current lock and package graph.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_NATIVE_DEVELOPMENT_RUNTIME_HPP
#define VIX_CLI_PROJECT_NATIVE_NATIVE_DEVELOPMENT_RUNTIME_HPP

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/project/native/AcquisitionMode.hpp>
#include <vix/cli/project/ProjectError.hpp>
#include <vix/cli/project/ProjectSnapshot.hpp>
#include <vix/cli/project/native/PackageBuildGraph.hpp>
#include <vix/cli/project/native/Lockfile.hpp>
#include <vix/cli/project/native/Models.hpp>

namespace vix::cli::project::native
{
  enum class NativeDevelopmentErrorCode
  {
    none,
    project_error,
    missing_lock,
    stale_lock,
    invalid_lock,
    package_preparation_failure,
    target_not_found,
    target_ambiguous,
    target_not_buildable,
    target_not_runnable,
    artifact_unavailable,
    test_not_found,
    invalid_test_registration,
    build_failure,
    process_execution_failure
  };

  struct NativeDevelopmentResult
  {
    int exitCode{1};
    std::string diagnostic;
    std::string output;
    NativeDevelopmentErrorCode code{NativeDevelopmentErrorCode::project_error};
    std::optional<project::ProjectError> projectError;
    [[nodiscard]] bool success() const noexcept { return exitCode == 0; }
  };

  struct NativeDevelopmentCallbackResult
  {
    bool success{false};
    int exitCode{1};
    std::optional<project::ProjectSnapshot> snapshot;
    std::string diagnostic;
    std::string output;
  };

  // Optional seams for focused runtime tests and embedders. Empty callbacks
  // retain the production CMake/process/P10Q implementation.
  struct NativeDevelopmentHooks
  {
    std::function<std::optional<std::vector<MaterializedLockedPackage>>(
        const std::filesystem::path &, const NativeProject &, const Lockfile &, AcquisitionMode, std::string &)> preparePackages;
    std::function<NativeDevelopmentCallbackResult(
        const std::filesystem::path &, const std::filesystem::path &, const std::filesystem::path &,
        const NativeProject &, const Lockfile &, const std::vector<MaterializedLockedPackage> &, std::string_view)> configure;
    std::function<NativeDevelopmentCallbackResult(
        const std::filesystem::path &, const std::filesystem::path &, const project::ProjectSnapshot &,
        const std::optional<std::string> &, int)> build;
    std::function<NativeDevelopmentCallbackResult(
        const std::vector<std::string> &, const std::filesystem::path &)> execute;
  };

  [[nodiscard]] NativeDevelopmentResult build_native_project(
    const std::filesystem::path &projectRoot,
    const std::optional<std::string> &target = std::nullopt,
    int jobs = 0,
    std::string_view preset = "dev",
    AcquisitionMode mode = AcquisitionMode::online,
    const NativeDevelopmentHooks *hooks = nullptr);

  [[nodiscard]] NativeDevelopmentResult run_native_project(
    const std::filesystem::path &projectRoot,
    const std::optional<std::string> &target,
    const std::vector<std::string> &arguments = {},
    std::string_view preset = "dev",
    AcquisitionMode mode = AcquisitionMode::online,
    const NativeDevelopmentHooks *hooks = nullptr);

  [[nodiscard]] NativeDevelopmentResult test_native_project(
    const std::filesystem::path &projectRoot,
    const std::optional<std::string> &testName = std::nullopt,
    bool listOnly = false,
    const std::vector<std::string> &ctestArguments = {},
    std::string_view testPattern = {},
    std::string_view preset = "dev",
    AcquisitionMode mode = AcquisitionMode::online,
    const NativeDevelopmentHooks *hooks = nullptr);
} // namespace vix::cli::project::native

#endif
