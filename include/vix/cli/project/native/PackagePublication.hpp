/** @file PackagePublication.hpp
 *  New-Vix Registry package publication preparation and submission boundary.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_PACKAGE_PUBLICATION_HPP
#define VIX_CLI_PROJECT_NATIVE_PACKAGE_PUBLICATION_HPP

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/project/native/PackageContentDigest.hpp>
#include <vix/cli/project/native/TomlParser.hpp>

namespace vix::cli::project::native
{
  struct PackagePublicationSubmission
  {
    RegistryPackageIdentity identity;
    Version version;
    PackageContract contract;
    PackageContentId contentId;
    std::string contractToml;
    std::string canonicalContent;
  };

  enum class PackagePublicationErrorCode
  {
    none,
    package_manifest_missing,
    package_manifest_invalid,
    non_registry_package,
    invalid_package_contract,
    content_collection_failed,
    publication_config_unavailable,
    publication_config_invalid,
    publication_target_not_configured,
    invalid_endpoint,
    credential_missing,
    unauthorized,
    version_conflict,
    invalid_package,
    content_mismatch,
    registry_error,
    transport_error,
  };

  struct PublicationTarget
  {
    std::string registryId;
    std::string endpoint;
    std::string tokenEnv;
  };

  struct RegistryPublicationDirectory
  {
    std::vector<PublicationTarget> targets;
    [[nodiscard]] const PublicationTarget *find(std::string_view registryId) const noexcept;
  };

  struct RegistryPublicationConfigurationResult
  {
    std::optional<RegistryPublicationDirectory> directory;
    PackagePublicationErrorCode code{PackagePublicationErrorCode::publication_config_invalid};
    std::string diagnostic;
    [[nodiscard]] bool valid() const noexcept
    { return directory.has_value() && diagnostic.empty() && code == PackagePublicationErrorCode::none; }
  };

  using PublicationTokenLookup = std::function<std::optional<std::string>(std::string_view)>;

  struct PackagePublicationResult
  {
    bool succeeded{false};
    PackagePublicationErrorCode code{PackagePublicationErrorCode::transport_error};
    std::optional<PackagePublicationSubmission> submission;
    std::string diagnostic;
    [[nodiscard]] bool valid() const noexcept { return succeeded && code == PackagePublicationErrorCode::none; }
  };

  enum class PublicationTransportStatus
  {
    accepted,
    already_exists_same_content,
    unauthorized,
    version_conflict,
    invalid_package,
    content_mismatch,
    registry_error,
    transport_error,
  };

  struct PublicationTransportResult
  {
    PublicationTransportStatus status{PublicationTransportStatus::transport_error};
    std::string diagnostic;
  };

  class PackagePublicationTransport
  {
  public:
    virtual ~PackagePublicationTransport() = default;
    [[nodiscard]] virtual PublicationTransportResult submit(
      const std::string &endpoint, const std::string &bearerToken,
      const PackagePublicationSubmission &submission) = 0;
  };

  class RequestsPackagePublicationTransport final : public PackagePublicationTransport
  {
  public:
    [[nodiscard]] PublicationTransportResult submit(
      const std::string &endpoint, const std::string &bearerToken,
      const PackagePublicationSubmission &submission) override;
  };

  [[nodiscard]] PackagePublicationResult prepare_package_publication(
    const std::filesystem::path &packageRoot);
  [[nodiscard]] std::filesystem::path registry_publication_configuration_path();
  [[nodiscard]] RegistryPublicationConfigurationResult load_registry_publication_directory(
    const std::filesystem::path &path);
  [[nodiscard]] PackagePublicationResult submit_package_publication(
    const std::filesystem::path &packageRoot, const RegistryPublicationDirectory &directory,
    const PublicationTokenLookup &tokenLookup, PackagePublicationTransport &transport);
  [[nodiscard]] PackagePublicationResult submit_package_publication(
    const std::filesystem::path &packageRoot, const PublicationTarget &target,
    const std::string &bearerToken, PackagePublicationTransport &transport);
  [[nodiscard]] int run_native_publish_command(
    const std::vector<std::string> &arguments,
    const std::filesystem::path &packageRoot = std::filesystem::current_path());
}

#endif
