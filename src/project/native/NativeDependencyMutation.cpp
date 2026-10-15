#include <vix/cli/project/native/NativeDependencyMutation.hpp>

#include <vix/cli/project/native/Resolver.hpp>
#include <vix/cli/project/native/Validation.hpp>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <system_error>
#include <utility>

namespace vix::cli::project::native
{
  namespace fs = std::filesystem;
  namespace
  {
    std::atomic<unsigned long long> transactionCounter{0};

    ProjectStateTransactionIssue transaction_issue(ProjectStateTransactionCode code,
      const fs::path &path, std::string message)
    {
      return {code, path.string(), std::move(message)};
    }

    class FilesystemProjectStateFileOperations final : public ProjectStateFileOperations
    {
    public:
      bool is_directory(const fs::path &path, std::error_code &error) const override
      { return fs::is_directory(path, error); }
      bool exists(const fs::path &path, std::error_code &error) const override
      { return fs::exists(path, error); }
      bool write_complete(const fs::path &path, std::string_view bytes) override
      {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
          return false;
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output)
          return false;
        output.close();
        return !output.fail();
      }
      void rename(const fs::path &from, const fs::path &to, std::error_code &error) override
      { fs::rename(from, to, error); }
      void remove(const fs::path &path, std::error_code &error) override
      { fs::remove(path, error); }
    };

    NativeDependencyMutationResult mutation_failure(NativeDependencyMutationCode code,
      std::string subject, std::string message)
    {
      NativeDependencyMutationResult result;
      result.issues.push_back({code, std::move(subject), std::move(message)});
      return result;
    }
  }

  NativeProjectStateTransaction::NativeProjectStateTransaction(fs::path projectRoot,
    ProjectStateFileOperations *fileOperations)
    : root_(std::move(projectRoot)), fileOperations_(fileOperations)
  {}

  ProjectStateTransactionResult NativeProjectStateTransaction::commit(
    bool writeManifest, const std::optional<std::string> &manifestText,
    const std::optional<std::string> &lockText)
  {
    ProjectStateTransactionResult result;
    FilesystemProjectStateFileOperations filesystemOperations;
    auto &operations = fileOperations_ ? *fileOperations_ :
      static_cast<ProjectStateFileOperations &>(filesystemOperations);
    const auto manifestPath = root_ / "vix.toml";
    const auto lockPath = root_ / "vix.lock";
    if ((writeManifest && !manifestText) || (manifestText && !writeManifest))
    {
      result.issues.push_back(transaction_issue(ProjectStateTransactionCode::invalid_candidate,
        manifestPath, "manifest publication requires exactly one validated candidate text"));
      return result;
    }

    std::error_code ec;
    if (!operations.is_directory(root_, ec) || ec)
    {
      result.issues.push_back(transaction_issue(ProjectStateTransactionCode::read_failure,
        root_, "project root is unavailable"));
      return result;
    }

    const auto token = ".vix-state-" + std::to_string(transactionCounter.fetch_add(1));
    const auto manifestTemp = root_ / ("vix.toml" + token + ".tmp");
    const auto lockTemp = root_ / ("vix.lock" + token + ".tmp");
    const auto manifestBackup = root_ / ("vix.toml" + token + ".bak");
    const auto lockBackup = root_ / ("vix.lock" + token + ".bak");
    bool manifestHadOld = false;
    bool lockHadOld = false;
    bool manifestBacked = false;
    bool lockBacked = false;
    bool manifestInstalled = false;
    bool lockInstalled = false;

    auto cleanup_temp = [&]
    {
      std::error_code ignored;
      operations.remove(manifestTemp, ignored);
      ignored.clear();
      operations.remove(lockTemp, ignored);
    };
    auto rollback = [&]() -> bool
    {
      bool restored = true;
      std::error_code rollbackError;
      if (manifestInstalled)
      {
        operations.remove(manifestPath, rollbackError);
        if (rollbackError) restored = false;
      }
      rollbackError.clear();
      if (lockInstalled)
      {
        operations.remove(lockPath, rollbackError);
        if (rollbackError) restored = false;
      }
      rollbackError.clear();
      if (manifestBacked)
      {
        operations.rename(manifestBackup, manifestPath, rollbackError);
        if (rollbackError) restored = false;
      }
      rollbackError.clear();
      if (lockBacked)
      {
        operations.rename(lockBackup, lockPath, rollbackError);
        if (rollbackError) restored = false;
      }
      cleanup_temp();
      return restored;
    };

    if (writeManifest && !operations.write_complete(manifestTemp, *manifestText))
    {
      cleanup_temp();
      result.issues.push_back(transaction_issue(ProjectStateTransactionCode::temporary_write_failure,
        manifestTemp, "could not write complete temporary manifest"));
      return result;
    }
    if (lockText && !operations.write_complete(lockTemp, *lockText))
    {
      cleanup_temp();
      result.issues.push_back(transaction_issue(ProjectStateTransactionCode::temporary_write_failure,
        lockTemp, "could not write complete temporary lockfile"));
      return result;
    }

    ec.clear();
    manifestHadOld = writeManifest && operations.exists(manifestPath, ec);
    if (ec)
    {
      cleanup_temp();
      result.issues.push_back(transaction_issue(ProjectStateTransactionCode::read_failure,
        manifestPath, "could not inspect existing manifest"));
      return result;
    }
    ec.clear();
    lockHadOld = operations.exists(lockPath, ec);
    if (ec)
    {
      cleanup_temp();
      result.issues.push_back(transaction_issue(ProjectStateTransactionCode::read_failure,
        lockPath, "could not inspect existing lockfile"));
      return result;
    }
    if (manifestHadOld)
    {
      operations.rename(manifestPath, manifestBackup, ec);
      if (ec)
      {
        cleanup_temp();
        result.issues.push_back(transaction_issue(ProjectStateTransactionCode::backup_failure,
          manifestPath, "could not preserve existing manifest"));
        return result;
      }
      manifestBacked = true;
    }
    if (lockHadOld)
    {
      operations.rename(lockPath, lockBackup, ec);
      if (ec)
      {
        const bool restored = rollback();
        result.issues.push_back(transaction_issue(restored ? ProjectStateTransactionCode::backup_failure :
          ProjectStateTransactionCode::rollback_failure, lockPath,
          restored ? "could not preserve existing lockfile" : "could not preserve lockfile and rollback failed"));
        return result;
      }
      lockBacked = true;
    }

    if (writeManifest)
    {
      operations.rename(manifestTemp, manifestPath, ec);
      if (ec)
      {
        const bool restored = rollback();
        result.issues.push_back(transaction_issue(restored ? ProjectStateTransactionCode::replacement_failure :
          ProjectStateTransactionCode::rollback_failure, manifestPath,
          restored ? "could not publish candidate manifest" : "manifest publication failed and rollback failed"));
        return result;
      }
      manifestInstalled = true;
    }
    if (lockText)
    {
      operations.rename(lockTemp, lockPath, ec);
      if (ec)
      {
        const bool restored = rollback();
        result.issues.push_back(transaction_issue(restored ? ProjectStateTransactionCode::replacement_failure :
          ProjectStateTransactionCode::rollback_failure, lockPath,
          restored ? "could not publish candidate lockfile" : "lockfile publication failed and rollback failed"));
        return result;
      }
      lockInstalled = true;
    }

    // Removing the previous lock is part of the no-lock candidate, not best-
    // effort backup cleanup. Do it while the old manifest backup is still
    // available so a failed removal can restore both original files.
    if (!lockText && lockBacked)
    {
      operations.remove(lockBackup, ec);
      if (ec)
      {
        const bool restored = rollback();
        result.issues.push_back(transaction_issue(restored ? ProjectStateTransactionCode::replacement_failure :
          ProjectStateTransactionCode::rollback_failure, lockPath,
          restored ? "could not remove previous lockfile" : "lockfile removal failed and rollback failed"));
        return result;
      }
      lockBacked = false;
    }

    ec.clear();
    if (manifestBacked) operations.remove(manifestBackup, ec);
    ec.clear();
    if (lockBacked) operations.remove(lockBackup, ec);
    cleanup_temp();
    result.committed = true;
    return result;
  }

  NativeDependencyMutationService::NativeDependencyMutationService(fs::path projectRoot,
    PackageMetadataSource &onlineMetadata, ProjectStateFileOperations *fileOperations)
    : root_(std::move(projectRoot)), onlineMetadata_(onlineMetadata), fileOperations_(fileOperations)
  {}

  NativeDependencyMutationResult NativeDependencyMutationService::add(PackageRequirement requirement)
  {
    const auto manifestPath = root_ / "vix.toml";
    auto parsed = parse_native_manifest_file(manifestPath);
    if (!parsed.valid())
      return mutation_failure(fs::exists(manifestPath) ? NativeDependencyMutationCode::invalid_project :
        NativeDependencyMutationCode::project_not_found, manifestPath.string(),
        parsed.issues.empty() ? "native project manifest is unavailable" : parsed.issues.front().message);
    auto candidate = std::move(*parsed.value);
    const auto mutation = add_root_package_requirement(candidate, std::move(requirement));
    if (!mutation.valid())
      return mutation_failure(mutation.issues.front().code == NativeProjectMutationErrorCode::duplicate_requirement_alias
        ? NativeDependencyMutationCode::requirement_already_exists : NativeDependencyMutationCode::invalid_requirement,
        mutation.issues.front().subject, mutation.issues.front().message);
    return resolve_and_commit(std::move(candidate), true);
  }

  NativeDependencyMutationResult NativeDependencyMutationService::remove(std::string_view rootAlias)
  {
    const auto manifestPath = root_ / "vix.toml";
    auto parsed = parse_native_manifest_file(manifestPath);
    if (!parsed.valid())
      return mutation_failure(fs::exists(manifestPath) ? NativeDependencyMutationCode::invalid_project :
        NativeDependencyMutationCode::project_not_found, manifestPath.string(),
        parsed.issues.empty() ? "native project manifest is unavailable" : parsed.issues.front().message);
    auto candidate = std::move(*parsed.value);
    const auto mutation = remove_root_package_requirement(candidate, rootAlias);
    if (!mutation.valid())
      return mutation_failure(mutation.issues.front().code == NativeProjectMutationErrorCode::requirement_not_found
        ? NativeDependencyMutationCode::requirement_not_found : NativeDependencyMutationCode::invalid_project,
        mutation.issues.front().subject, mutation.issues.front().message);
    if (candidate.packages.empty())
      return commit_empty(std::move(candidate));
    return resolve_and_commit(std::move(candidate), true);
  }

  NativeDependencyMutationResult NativeDependencyMutationService::remove_identity(const PackageIdentity &identity)
  {
    const auto manifestPath = root_ / "vix.toml";
    auto parsed = parse_native_manifest_file(manifestPath);
    if (!parsed.valid())
      return mutation_failure(fs::exists(manifestPath) ? NativeDependencyMutationCode::invalid_project :
        NativeDependencyMutationCode::project_not_found, manifestPath.string(),
        parsed.issues.empty() ? "native project manifest is unavailable" : parsed.issues.front().message);
    const auto found = std::find_if(parsed.value->packages.begin(), parsed.value->packages.end(),
      [&](const PackageRequirement &requirement)
      {
        if (const auto *registry = std::get_if<RegistryRequirement>(&requirement.source))
        {
          const auto *requested = std::get_if<RegistryPackageIdentity>(&identity);
          return requested && registry->identity == *requested;
        }
        const auto &git = std::get<GitRequirement>(requirement.source);
        const auto *requested = std::get_if<GitPackageIdentity>(&identity);
        return requested && git.normalizedRepository == requested->normalizedRepository &&
          git.repositoryRoot == requested->repositoryRoot;
      });
    if (found == parsed.value->packages.end())
      return mutation_failure(NativeDependencyMutationCode::requirement_not_found, "root requirement",
        "the requested package identity is not a root requirement");
    return remove(found->alias);
  }

  NativeDependencyMutationResult NativeDependencyMutationService::update()
  {
    const auto manifestPath = root_ / "vix.toml";
    auto parsed = parse_native_manifest_file(manifestPath);
    if (!parsed.valid())
      return mutation_failure(fs::exists(manifestPath) ? NativeDependencyMutationCode::invalid_project :
        NativeDependencyMutationCode::project_not_found, manifestPath.string(),
        parsed.issues.empty() ? "native project manifest is unavailable" : parsed.issues.front().message);
    if (parsed.value->packages.empty())
    {
      NativeProjectStateTransaction transaction{root_, fileOperations_};
      const auto persisted = transaction.commit(false, std::nullopt, std::nullopt);
      if (!persisted.valid())
        return mutation_failure(NativeDependencyMutationCode::persistence_failure, "vix.lock",
          persisted.issues.empty() ? "could not remove stale lockfile" : persisted.issues.front().message);
      NativeDependencyMutationResult result;
      result.succeeded = true;
      return result;
    }
    return resolve_and_commit(std::move(*parsed.value), false);
  }

  NativeDependencyMutationResult NativeDependencyMutationService::resolve_and_commit(
    NativeProject candidate, bool writeManifest)
  {
    const auto validation = validate_native_project(candidate);
    if (!validation.valid())
      return mutation_failure(NativeDependencyMutationCode::invalid_project,
        validation.issues.front().subject, validation.issues.front().message);
    const auto metadata = acquire_resolution_catalog(candidate.packages, onlineMetadata_);
    if (!metadata.valid())
      return mutation_failure(NativeDependencyMutationCode::metadata_failure,
        metadata.issues.empty() ? "metadata" : metadata.issues.front().subject,
        metadata.issues.empty() ? "online metadata acquisition failed" : metadata.issues.front().message);
    const auto graph = resolve_packages(candidate.packages, *metadata.catalog);
    if (!graph.valid())
      return mutation_failure(NativeDependencyMutationCode::resolution_failure,
        graph.issues.empty() ? "dependencies" : graph.issues.front().identity,
        graph.issues.empty() ? "dependency resolution failed" : graph.issues.front().message);
    const auto lock = make_resolved_lockfile(*graph.value);
    if (!lock.valid())
      return mutation_failure(NativeDependencyMutationCode::lock_failure,
        lock.issues.empty() ? "vix.lock" : lock.issues.front().identity,
        lock.issues.empty() ? "could not construct a valid lockfile" : lock.issues.front().message);
    const auto serializedLock = serialize_lockfile(*lock.value);
    if (!serializedLock.valid())
      return mutation_failure(NativeDependencyMutationCode::lock_failure, "vix.lock",
        serializedLock.issues.empty() ? "could not serialize lockfile" : serializedLock.issues.front().message);
    std::optional<std::string> serializedManifest;
    if (writeManifest)
    {
      const auto serialized = serialize_native_manifest(candidate);
      if (!serialized.valid())
        return mutation_failure(NativeDependencyMutationCode::invalid_project, "vix.toml",
          serialized.issues.empty() ? "could not serialize project manifest" : serialized.issues.front().message);
      serializedManifest = *serialized.text;
    }
    NativeProjectStateTransaction transaction{root_, fileOperations_};
    const auto persisted = transaction.commit(writeManifest, serializedManifest, *serializedLock.text);
    if (!persisted.valid())
      return mutation_failure(NativeDependencyMutationCode::persistence_failure,
        persisted.issues.empty() ? "project state" : persisted.issues.front().path,
        persisted.issues.empty() ? "could not persist project state" : persisted.issues.front().message);
    NativeDependencyMutationResult result;
    result.succeeded = true;
    return result;
  }

  NativeDependencyMutationResult NativeDependencyMutationService::commit_empty(NativeProject candidate)
  {
    const auto validation = validate_native_project(candidate);
    if (!validation.valid())
      return mutation_failure(NativeDependencyMutationCode::invalid_project,
        validation.issues.front().subject, validation.issues.front().message);
    const auto serialized = serialize_native_manifest(candidate);
    if (!serialized.valid())
      return mutation_failure(NativeDependencyMutationCode::invalid_project, "vix.toml",
        serialized.issues.empty() ? "could not serialize project manifest" : serialized.issues.front().message);
    NativeProjectStateTransaction transaction{root_, fileOperations_};
    const auto persisted = transaction.commit(true, *serialized.text, std::nullopt);
    if (!persisted.valid())
      return mutation_failure(NativeDependencyMutationCode::persistence_failure,
        persisted.issues.empty() ? "project state" : persisted.issues.front().path,
        persisted.issues.empty() ? "could not persist project state" : persisted.issues.front().message);
    NativeDependencyMutationResult result;
    result.succeeded = true;
    return result;
  }
}
