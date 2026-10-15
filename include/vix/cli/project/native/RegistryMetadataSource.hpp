/** @file RegistryMetadataSource.hpp
 *  Registry Metadata Transport v1 and snapshot commitment implementation.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_REGISTRY_METADATA_SOURCE_HPP
#define VIX_CLI_PROJECT_NATIVE_REGISTRY_METADATA_SOURCE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <vix/cli/project/native/RegistryAuthority.hpp>
#include <vix/cli/project/native/MetadataStorage.hpp>

namespace vix::cli::project::native
{
  class RegistryHttpsRequestExecutor;

  struct RegistryMetadataTrustedKey
  {
    std::string keyId;
    std::string publicKeyHex;
  };
  struct RegistryMetadataTrustedAuthority
  {
    std::string authorityId;
    std::vector<RegistryMetadataTrustedKey> keys;
  };

  [[nodiscard]] std::string registry_observation_content_digest(
    const RegistryPackageIdentity &query, std::uint64_t sequence,
    const std::vector<RegistryCandidate> &releases);
  [[nodiscard]] std::string registry_snapshot_manifest_digest(
    std::string_view registryId, std::uint64_t sequence,
    const std::vector<std::tuple<std::string, std::string, std::string>> &packages);
  [[nodiscard]] std::string registry_head_signature_payload(
    const RegistrySnapshotIdentity &snapshot, std::string_view authorityId, std::string_view keyId);

  class Ed25519RegistryAuthorityVerifier final : public RegistryAuthorityVerifier
  {
  public:
    explicit Ed25519RegistryAuthorityVerifier(std::vector<RegistryMetadataTrustedAuthority> authorities);
    [[nodiscard]] bool verify_evidence(const RegistryAuthorityDefinition &authority,
      const UnverifiedRegistryObservation &observation, std::string &diagnostic) const override;
  private:
    std::vector<RegistryMetadataTrustedAuthority> authorities_;
  };

  struct RegistryMetadataTransportResult
  {
    RegistryObservationLocationStatus status{RegistryObservationLocationStatus::transport_failure};
    std::optional<std::string> bytes;
    std::string diagnostic;
  };
  class RegistryMetadataTransport
  {
  public:
    virtual ~RegistryMetadataTransport() = default;
    [[nodiscard]] virtual RegistryMetadataTransportResult fetch(const RegistryLocation &location,
      std::string_view relativePath, std::uint64_t maximumBytes) = 0;
  };

  class RequestsRegistryMetadataTransport final : public RegistryMetadataTransport
  {
  public:
    explicit RequestsRegistryMetadataTransport(RegistryHttpsRequestExecutor &executor) : executor_(executor) {}
    [[nodiscard]] RegistryMetadataTransportResult fetch(const RegistryLocation &location,
      std::string_view relativePath, std::uint64_t maximumBytes) override;
  private:
    RegistryHttpsRequestExecutor &executor_;
  };

  class TransportRegistryObservationLocationSource final : public RegistryObservationLocationSource
  {
  public:
    explicit TransportRegistryObservationLocationSource(RegistryMetadataTransport &transport,
      std::uint64_t maximumBytes = 16ULL * 1024ULL * 1024ULL)
      : transport_(transport), maximumBytes_(maximumBytes) {}
    [[nodiscard]] UnverifiedRegistryLocationResult registry_observation_at(
      const RegistryLocation &location, const RegistryPackageIdentity &identity) override;
  private:
    RegistryMetadataTransport &transport_;
    std::uint64_t maximumBytes_;
  };

  class RegistryOnlinePackageMetadataSource final : public PackageMetadataSource
  {
  public:
    RegistryOnlinePackageMetadataSource(const RegistryDirectory &locations,
      const RegistryAuthorityDirectory &authorities, RegistryObservationLocationSource &source,
      const RegistryAuthorityVerifier &verifier, TrustedRegistryStateStore &trustedStates,
      PackageMetadataCache &cache)
      : locations_(locations), authorities_(authorities), source_(source), verifier_(verifier),
        trustedStates_(trustedStates), cache_(cache) {}
    [[nodiscard]] RegistryQueryResult registry_releases(const RegistryPackageIdentity &identity) override;
    [[nodiscard]] GitQueryResult resolve_git(const GitPackageIdentity &,
      const GitRevisionRequirement &) override;
  private:
    const RegistryDirectory &locations_;
    const RegistryAuthorityDirectory &authorities_;
    RegistryObservationLocationSource &source_;
    const RegistryAuthorityVerifier &verifier_;
    TrustedRegistryStateStore &trustedStates_;
    PackageMetadataCache &cache_;
    std::vector<RegistrySnapshotPin> pins_;
  };
}

#endif
