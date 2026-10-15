/** @file PackageCommandRuntime.hpp
 *  P10S command grammar and production online metadata runtime composition.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_PACKAGE_COMMAND_RUNTIME_HPP
#define VIX_CLI_PROJECT_NATIVE_PACKAGE_COMMAND_RUNTIME_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/project/native/MetadataAcquisition.hpp>
#include <vix/cli/project/native/RegistryMetadataSource.hpp>
#include <vix/cli/project/native/GitPackageMetadataSource.hpp>

namespace vix::cli::project::native
{
  enum class PackageCommandParseCode { usage, invalid_identity, invalid_constraint, invalid_revision };
  struct PackageCommandParseIssue { PackageCommandParseCode code; std::string message; };
  template <typename T>
  struct PackageCommandParseResult
  {
    std::optional<T> value;
    std::vector<PackageCommandParseIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return value.has_value() && issues.empty(); }
  };

  struct ParsedAddCommand
  {
    std::optional<PackageRequirement> requirement;
    std::optional<RegistryPackageIdentity> implicitStableRegistry;
    std::optional<GitPackageIdentity> implicitDefaultBranch;
    std::string implicitAlias;
  };
  struct ParsedRemoveCommand { PackageIdentity identity; };

  [[nodiscard]] PackageCommandParseResult<ParsedAddCommand> parse_native_add_arguments(
    const std::vector<std::string> &arguments);
  [[nodiscard]] PackageCommandParseResult<ParsedRemoveCommand> parse_native_remove_arguments(
    const std::vector<std::string> &arguments);
  [[nodiscard]] PackageCommandParseResult<bool> parse_native_update_arguments(
    const std::vector<std::string> &arguments);

  struct RegistryRuntimeConfiguration
  {
    std::vector<RegistryDefinition> registries;
    std::vector<RegistryAuthorityDefinition> authorities;
    std::vector<RegistryMetadataTrustedAuthority> trustedKeys;
  };
  struct RegistryRuntimeConfigurationResult
  {
    std::optional<RegistryRuntimeConfiguration> configuration;
    std::string diagnostic;
    [[nodiscard]] bool valid() const noexcept { return configuration.has_value() && diagnostic.empty(); }
  };

  [[nodiscard]] std::filesystem::path registry_runtime_configuration_path();
  [[nodiscard]] std::filesystem::path metadata_state_path();
  [[nodiscard]] RegistryRuntimeConfigurationResult load_registry_runtime_configuration(
    const std::filesystem::path &path);

  class OnlinePackageMetadataRuntime final : public GitDefaultBranchSource
  {
  public:
    ~OnlinePackageMetadataRuntime();
    OnlinePackageMetadataRuntime(OnlinePackageMetadataRuntime &&) noexcept;
    OnlinePackageMetadataRuntime &operator=(OnlinePackageMetadataRuntime &&) noexcept;
    OnlinePackageMetadataRuntime(const OnlinePackageMetadataRuntime &) = delete;
    OnlinePackageMetadataRuntime &operator=(const OnlinePackageMetadataRuntime &) = delete;

    [[nodiscard]] static std::unique_ptr<OnlinePackageMetadataRuntime> create(
      const RegistryRuntimeConfiguration &configuration,
      const std::filesystem::path &metadataRoot,
      const std::filesystem::path &gitWorkRoot,
      std::string &diagnostic);
    [[nodiscard]] PackageMetadataSource &source() noexcept;
    [[nodiscard]] bool has_registry(std::string_view registryId) const noexcept;
    [[nodiscard]] GitDefaultBranchResult discover_default_branch(
      const GitPackageIdentity &identity) override;
  private:
    struct Impl;
    explicit OnlinePackageMetadataRuntime(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
  };

  [[nodiscard]] int run_native_add_command(const std::vector<std::string> &arguments,
    const std::filesystem::path &workingDirectory = std::filesystem::current_path());
  // Injectable command boundary for deterministic CLI integration tests.
  [[nodiscard]] int run_native_add_command_with_source(const std::vector<std::string> &arguments,
    const std::filesystem::path &workingDirectory, PackageMetadataSource &source,
    GitDefaultBranchSource *gitDefaults = nullptr);
  [[nodiscard]] int run_native_remove_command(const std::vector<std::string> &arguments,
    const std::filesystem::path &workingDirectory = std::filesystem::current_path());
  [[nodiscard]] int run_native_remove_command_with_source(const std::vector<std::string> &arguments,
    const std::filesystem::path &workingDirectory, PackageMetadataSource &source);
  [[nodiscard]] int run_native_update_command(const std::vector<std::string> &arguments,
    const std::filesystem::path &workingDirectory = std::filesystem::current_path());
  [[nodiscard]] int run_native_update_command_with_source(const std::vector<std::string> &arguments,
    const std::filesystem::path &workingDirectory, PackageMetadataSource &source);
}

#endif
