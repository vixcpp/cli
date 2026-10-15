#include <vix/cli/project/native/NativePackageInstall.hpp>

#include <vix/cli/project/native/GitPackageContentSource.hpp>
#include <vix/cli/project/native/PackageCommandRuntime.hpp>
#include <vix/cli/project/native/RegistryContentTransport.hpp>
#include <vix/cli/project/native/TomlParser.hpp>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace vix::cli::project::native
{
  namespace fs = std::filesystem;

  fs::path package_content_store_path()
  {
    if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
      return fs::path{xdg} / "vix" / "package-content";
    if (const char *home = std::getenv("HOME"); home && *home)
      return fs::path{home} / ".cache" / "vix" / "package-content";
    return {};
  }

  int run_native_install_command(const std::vector<std::string> &arguments, const fs::path &workingDirectory)
  {
    const auto parsedArguments = parse_native_install_arguments(arguments);
    if (!parsedArguments.valid)
    {
      std::cerr << "vix: " << parsedArguments.diagnostic << '\n';
      return 2;
    }
    const auto manifest = workingDirectory / "vix.toml";
    if (!fs::is_regular_file(manifest))
    {
      std::cerr << "vix: native-project-required (vix.toml not found)\n";
      return 1;
    }
    const auto project = parse_native_manifest_file(manifest);
    if (!project.valid())
    {
      std::cerr << "vix: invalid vix.toml: " <<
        (project.issues.empty() ? "invalid project" : project.issues.front().message) << '\n';
      return 1;
    }
    const auto lockPath = workingDirectory / "vix.lock";
    std::optional<Lockfile> lock;
    if (fs::exists(lockPath))
    {
      const auto parsedLock = parse_lockfile_file(lockPath);
      if (!parsedLock.valid())
      {
        std::cerr << "vix: install error " << static_cast<int>(NativePackageInstallIssueCode::lock_invalid)
          << " [" << lockPath.string() << "]: " <<
          (parsedLock.issues.empty() ? "vix.lock is invalid" : parsedLock.issues.front().message) << '\n';
        return 1;
      }
      lock = *parsedLock.value;
    }
    const auto lockStatus = evaluate_project_lock_state(*project.value, lock);
    if (lockStatus.state == ProjectLockState::not_required)
    { std::cout << "nothing to install\n"; return 0; }
    if (lockStatus.state != ProjectLockState::current)
    {
      const auto code = lockStatus.state == ProjectLockState::missing ? NativePackageInstallIssueCode::lock_missing :
        lockStatus.state == ProjectLockState::stale ? NativePackageInstallIssueCode::lock_stale :
        lockStatus.state == ProjectLockState::unnecessary ? NativePackageInstallIssueCode::lock_unnecessary :
        NativePackageInstallIssueCode::lock_invalid;
      const auto detail = lockStatus.issues.empty() ? std::string{} : ": " + lockStatus.issues.front().message;
      std::cerr << "vix: install error " << static_cast<int>(code) << " [" << lockPath.string() << "]: "
        << "install requires a valid current lock; lock state is " << static_cast<int>(lockStatus.state) << detail << '\n';
      return 1;
    }

    const auto configPath = registry_runtime_configuration_path();
    const auto configuration = load_registry_runtime_configuration(configPath);
    if (!configuration.valid())
    {
      std::cerr << "vix: invalid Registry runtime configuration: " << configuration.diagnostic << '\n';
      return 1;
    }
    auto definitions = configuration.configuration->registries;
    for (auto &definition : definitions)
      for (const auto &location : definition.locations)
        definition.contentLocations.push_back({location.descriptor});
    auto registryBuild = build_registry_directory(std::move(definitions));
    if (!registryBuild.valid())
    {
      const std::string diagnostic = registryBuild.issues.empty() ? "invalid Registry content directory" :
        registryBuild.issues.front().message;
      std::cerr << "vix: invalid Registry content configuration: " << diagnostic << '\n';
      return 1;
    }

    const auto storePath = package_content_store_path();
    if (storePath.empty())
    {
      std::cerr << "vix: cannot determine package content store path; set HOME or XDG_CACHE_HOME\n";
      return 1;
    }
    try
    {
      RequestsRegistryHttpsRequestExecutor httpsExecutor;
      SchemeRegistryContentTransport transport{httpsExecutor};
      TransportRegistryPackageContentSource registrySource{transport};
      NativeGitCommandRunner gitRunner;
      GitCliPackageContentSource gitSource{fs::temp_directory_path() / "vix-git-content-install", gitRunner};
      FilesystemPackageContentStore store{storePath};
      auto result = install_native_project_packages(workingDirectory, AcquisitionMode::online,
        *registryBuild.value, registrySource, gitSource, store, workingDirectory / ".vix" / "packages");
      if (!result.valid())
      {
        const auto &issue = result.issues.front();
        std::cerr << "vix: install error " << static_cast<int>(issue.code) << " [" << issue.subject << "]: "
          << issue.message << '\n';
        return 1;
      }
      std::cout << "locked packages installed\n";
      return 0;
    }
    catch (const std::exception &error)
    {
      std::cerr << "vix: install runtime failure: " << error.what() << '\n';
      return 1;
    }
  }
} // namespace vix::cli::project::native
