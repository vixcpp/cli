/** @file RegistryContentTransport.hpp
 *  Local Registry format-1 content mirror transport.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_REGISTRY_CONTENT_TRANSPORT_HPP
#define VIX_CLI_PROJECT_NATIVE_REGISTRY_CONTENT_TRANSPORT_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <vix/cli/project/native/PackageContentAcquisition.hpp>

namespace vix::cli::project::native
{
  inline constexpr std::uint64_t default_registry_content_maximum_bytes =
    1024ULL * 1024ULL * 1024ULL;

  using RegistryContentTransportStatus = RegistryPackageContentSourceStatus;

  struct RegistryContentTransportResult
  {
    RegistryContentTransportStatus status{RegistryContentTransportStatus::not_found};
    std::optional<std::string> bytes;
    std::string diagnostic;
  };

  // Fetches one digest-addressed object from one configured mirror. It does
  // not iterate mirrors or validate P10O content.
  class RegistryContentTransport
  {
  public:
    virtual ~RegistryContentTransport() = default;
    [[nodiscard]] virtual RegistryContentTransportResult fetch(
      const RegistryContentLocation &location,
      std::string_view relativeObjectPath,
      std::uint64_t maximumBytes) = 0;
  };

  class FileRegistryContentTransport final : public RegistryContentTransport
  {
  public:
    [[nodiscard]] RegistryContentTransportResult fetch(
      const RegistryContentLocation &location,
      std::string_view relativeObjectPath,
      std::uint64_t maximumBytes) override;
  };

  // Adapts a single-location transport to the P10Q-A source boundary. P10Q-A
  // retains ownership of mirror fallback; P10O/P10P retain integrity/storage.
  class TransportRegistryPackageContentSource final : public RegistryPackageContentSource
  {
  public:
    explicit TransportRegistryPackageContentSource(
      RegistryContentTransport &transport,
      std::uint64_t maximumBytes = default_registry_content_maximum_bytes)
      : transport_(transport), maximumBytes_(maximumBytes) {}

    [[nodiscard]] RegistryPackageContentSourceResult acquire(
      const RegistryContentLocation &location,
      const RegistryPackageContentRequest &request) override;

  private:
    RegistryContentTransport &transport_;
    std::uint64_t maximumBytes_;
  };
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_REGISTRY_CONTENT_TRANSPORT_HPP
