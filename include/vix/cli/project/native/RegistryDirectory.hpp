/** @file RegistryDirectory.hpp
 *  Logical Registry namespace directory and ordered metadata routing boundary.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_REGISTRY_DIRECTORY_HPP
#define VIX_CLI_PROJECT_NATIVE_REGISTRY_DIRECTORY_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/project/native/MetadataAcquisition.hpp>

namespace vix::cli::project::native
{
  // Opaque routing descriptor. It is never part of Registry/package identity
  // and is not interpreted as a URL or transport by this layer.
  struct RegistryLocation
  {
    std::string descriptor;
    friend bool operator==(const RegistryLocation &, const RegistryLocation &) = default;
  };

  struct RegistryDefinition
  {
    std::string id;
    // Declaration order is fallback preference, not package semantics.
    std::vector<RegistryLocation> locations;
  };

  enum class RegistryDirectoryErrorCode
  {
    invalid_registry_id,
    duplicate_registry_id,
    no_registry_locations,
    invalid_location,
    duplicate_location
  };

  struct RegistryDirectoryIssue
  {
    RegistryDirectoryErrorCode code{RegistryDirectoryErrorCode::invalid_registry_id};
    std::string subject;
    std::string message;
  };

  class RegistryDirectory;
  struct RegistryDirectoryBuildResult;
  [[nodiscard]] RegistryDirectoryBuildResult build_registry_directory(std::vector<RegistryDefinition> definitions);

  class RegistryDirectory
  {
  public:
    [[nodiscard]] const RegistryDefinition *find(std::string_view registryId) const noexcept;

  private:
    friend RegistryDirectoryBuildResult build_registry_directory(std::vector<RegistryDefinition>);
    explicit RegistryDirectory(std::vector<RegistryDefinition> definitions);
    std::vector<RegistryDefinition> definitions_;
  };

  struct RegistryDirectoryBuildResult
  {
    std::optional<RegistryDirectory> value;
    std::vector<RegistryDirectoryIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return value.has_value() && issues.empty(); }
  };

  class RegistryMetadataLocationSource
  {
  public:
    virtual ~RegistryMetadataLocationSource() = default;
    [[nodiscard]] virtual RegistryQueryResult registry_releases_at(
      const RegistryLocation &location,
      const RegistryPackageIdentity &identity) = 0;
  };

  enum class RegistryRoutingErrorCode
  {
    unknown_registry,
    location_failure,
    invalid_location_response
  };

  struct RegistryRoutingIssue
  {
    RegistryRoutingErrorCode code{RegistryRoutingErrorCode::location_failure};
    std::string registryId;
    std::string location;
    std::string message;
  };

  struct RegistryRoutingResult
  {
    std::optional<RegistryQueryResult> observation;
    std::vector<RegistryRoutingIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return observation.has_value() && issues.empty(); }
  };

  // Tries configured alternatives in declaration order and returns the first
  // successful observation. A successful empty response is final; mirrors are
  // never unioned. The returned observation remains bound to the logical ID.
  [[nodiscard]] RegistryRoutingResult route_registry_releases(
    const RegistryDirectory &directory,
    RegistryMetadataLocationSource &source,
    const RegistryPackageIdentity &identity);

  // Adapts Registry routing plus an existing metadata source for Git queries
  // to P10H's source interface. No transport or authentication is implemented.
  class RoutedPackageMetadataSource final : public PackageMetadataSource
  {
  public:
    RoutedPackageMetadataSource(const RegistryDirectory &directory,
                                RegistryMetadataLocationSource &registrySource,
                                PackageMetadataSource &gitSource);

    [[nodiscard]] RegistryQueryResult registry_releases(
      const RegistryPackageIdentity &identity) override;
    [[nodiscard]] GitQueryResult resolve_git(
      const GitPackageIdentity &identity,
      const GitRevisionRequirement &revision) override;

  private:
    const RegistryDirectory &directory_;
    RegistryMetadataLocationSource &registrySource_;
    PackageMetadataSource &gitSource_;
  };
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_REGISTRY_DIRECTORY_HPP
