#include <vix/cli/project/native/NativePackageInstall.hpp>

#include <vix/cli/project/native/TomlParser.hpp>

#include <filesystem>
#include <string>
#include <utility>

namespace vix::cli::project::native
{
  namespace fs = std::filesystem;
  namespace
  {
    void add_issue(NativePackageInstallResult &result, NativePackageInstallIssueCode code,
      std::string subject, std::string message)
    { result.issues.push_back({code, std::move(subject), std::move(message)}); }

    const char *lock_state_name(ProjectLockState state)
    {
      switch (state)
      {
        case ProjectLockState::not_required: return "not_required";
        case ProjectLockState::missing: return "missing";
        case ProjectLockState::current: return "current";
        case ProjectLockState::stale: return "stale";
        case ProjectLockState::invalid: return "invalid";
        case ProjectLockState::unnecessary: return "unnecessary";
      }
      return "invalid";
    }

    std::optional<Lockfile> read_lock(const fs::path &path, std::string &diagnostic)
    {
      if (!fs::exists(path)) return std::nullopt;
      auto parsed = parse_lockfile_file(path);
      if (!parsed.valid())
      {
        diagnostic = parsed.issues.empty() ? "lockfile could not be parsed" : parsed.issues.front().message;
        return std::nullopt;
      }
      return std::move(*parsed.value);
    }

  } // namespace

  NativePackageInstallParseResult parse_native_install_arguments(
    const std::vector<std::string> &arguments)
  {
    if (!arguments.empty())
      return {false, "usage: vix install (package selection belongs to vix add)"};
    return {true, {}};
  }

  NativePackageInstallResult install_native_project_packages(
    const fs::path &projectRoot, AcquisitionMode mode, const RegistryDirectory &registries,
    RegistryPackageContentSource &registrySource, GitPackageContentSource &gitSource,
    FilesystemPackageContentStore &store, const fs::path &materializationRoot)
  {
    NativePackageInstallResult result;
    const auto manifestPath = projectRoot / "vix.toml";
    if (!fs::is_regular_file(manifestPath))
    {
      add_issue(result, NativePackageInstallIssueCode::project_not_found, manifestPath.string(),
        "native project is required (vix.toml not found)");
      return result;
    }
    const auto project = parse_native_manifest_file(manifestPath);
    if (!project.valid())
    {
      add_issue(result, NativePackageInstallIssueCode::invalid_project, manifestPath.string(),
        project.issues.empty() ? "vix.toml is invalid" : project.issues.front().message);
      return result;
    }

    const auto lockPath = projectRoot / "vix.lock";
    std::string lockDiagnostic;
    std::optional<Lockfile> lock;
    if (fs::exists(lockPath)) lock = read_lock(lockPath, lockDiagnostic);
    if (fs::exists(lockPath) && !lock)
    {
      add_issue(result, NativePackageInstallIssueCode::lock_invalid, lockPath.string(),
        lockDiagnostic.empty() ? "vix.lock is invalid" : lockDiagnostic);
      return result;
    }

    const auto status = evaluate_project_lock_state(*project.value, lock);
    if (status.state == ProjectLockState::not_required) return result;
    if (status.state != ProjectLockState::current)
    {
      const auto code = status.state == ProjectLockState::missing ? NativePackageInstallIssueCode::lock_missing :
        status.state == ProjectLockState::stale ? NativePackageInstallIssueCode::lock_stale :
        status.state == ProjectLockState::unnecessary ? NativePackageInstallIssueCode::lock_unnecessary :
        NativePackageInstallIssueCode::lock_invalid;
      const std::string detail = status.issues.empty() ? std::string{} : ": " + status.issues.front().message;
      add_issue(result, code, lockPath.string(),
        std::string{"install requires a valid current lock; lock state is "} + lock_state_name(status.state) + detail);
      return result;
    }

    auto acquired = acquire_locked_package_content(*lock, mode, registries, registrySource, gitSource, store);
    if (!acquired.valid())
    {
      const auto &failure = acquired.issues.front();
      std::string message = failure.message;
      if (!failure.nodeIds.empty())
      {
        message += " (locked node";
        if (failure.nodeIds.size() != 1) message += 's';
        message += ": ";
        for (std::size_t index = 0; index < failure.nodeIds.size(); ++index)
        {
          if (index != 0) message += ", ";
          message += failure.nodeIds[index];
        }
        message += ')';
      }
      add_issue(result, NativePackageInstallIssueCode::content_acquisition_failed,
        failure.digest.empty() ? lockPath.string() : failure.digest, std::move(message));
      return result;
    }

    const auto plan = required_package_content(*lock);
    if (!plan.valid())
    {
      add_issue(result, NativePackageInstallIssueCode::lock_invalid, lockPath.string(),
        "could not derive content plan from valid lock");
      return result;
    }
    // Materialization starts only after the complete lock content plan has
    // been verified/admitted in P10P.
    for (const auto &required : plan.required)
    {
      auto materialized = materialize_package_content(required.id, store, materializationRoot);
      if (!materialized.valid())
      {
        const auto message = materialized.issues.empty() ? "package materialization failed" :
          materialized.issues.front().message;
        std::string packageMessage = message;
        if (!required.nodeIds.empty()) packageMessage += " (locked node: " + required.nodeIds.front() + ')';
        add_issue(result, NativePackageInstallIssueCode::materialization_failed,
          required.id.digest(), std::move(packageMessage));
        return result;
      }
      result.materializations.push_back(std::move(materialized));
    }
    result.content = std::move(acquired.entries);
    return result;
  }

} // namespace vix::cli::project::native
