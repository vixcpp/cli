/** @file RegistryDirectory.hpp
 *  Logical Registry namespace directory and ordered metadata routing boundary.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_REGISTRY_DIRECTORY_HPP
#define VIX_CLI_PROJECT_NATIVE_REGISTRY_DIRECTORY_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/project/native/Models.hpp>

namespace vix::cli::project::native
{
  // Opaque routing descriptor. It is never part of Registry/package identity
  // and is not interpreted as a URL or transport by this layer.
  struct RegistryLocation
  {
    std::string descriptor;
    friend bool operator==(const RegistryLocation &, const RegistryLocation &) = default;
  };

  // Opaque transport configuration for immutable Registry package content.
  // This is deliberately distinct from metadata-observation routing.
  struct RegistryContentLocation
  {
    std::string descriptor;
    friend bool operator==(const RegistryContentLocation &, const RegistryContentLocation &) = default;
  };

  struct RegistryDefinition
  {
    std::string id;
    // Declaration order is fallback preference, not package semantics.
    std::vector<RegistryLocation> locations;
    // Independent content mirrors; declaration order is fallback preference.
    // Empty is valid when this Registry serves metadata but no content mirrors.
    std::vector<RegistryContentLocation> contentLocations;
  };

  enum class RegistryDirectoryErrorCode
  {
    invalid_registry_id,
    duplicate_registry_id,
    no_registry_locations,
    invalid_location,
    duplicate_location,
    invalid_content_location,
    duplicate_content_location
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

} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_REGISTRY_DIRECTORY_HPP
