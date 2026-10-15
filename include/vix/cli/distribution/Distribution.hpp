#ifndef VIX_CLI_DISTRIBUTION_DISTRIBUTION_HPP
#define VIX_CLI_DISTRIBUTION_DISTRIBUTION_HPP

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vix/cli/project/native/Models.hpp>

namespace vix::cli::distribution
{
  namespace detail { struct VerifiedReleaseManifestAccess; }

  enum class ErrorCode
  {
    invalid_value,
    unsupported_host,
    malformed_manifest,
    invalid_artifact_digest,
    unknown_key,
    unsupported_algorithm,
    invalid_signature,
    wrong_generation,
    invalid_receipt,
    externally_managed,
    unsupported_host_artifact,
    unsupported_components,
    channel_mismatch,
    no_newer_release,
    release_source_missing,
    manifest_fetch_failed,
    signature_fetch_failed,
    artifact_fetch_failed,
    artifact_digest_mismatch,
    unsupported_archive,
    unsafe_archive,
    invalid_distribution,
    staging_failed,
    activation_failed,
    rollback_failed,
    unmanaged_install
  };

  struct Error
  {
    ErrorCode code{ErrorCode::invalid_value};
    std::string message;
  };

  struct Unit {};

  template <class T>
  struct Result
  {
    std::optional<T> value;
    std::optional<Error> error;
    bool valid() const noexcept { return value.has_value() && !error.has_value(); }
  };

  struct HostPlatform
  {
    std::string architecture;
    std::string operatingSystem;
    std::string abi;
    std::string canonical() const;
    friend bool operator==(const HostPlatform &, const HostPlatform &) = default;
  };

  Result<HostPlatform> parse_host_platform(const std::string &value);
  Result<HostPlatform> detect_host_platform();

  struct TargetPlatform
  {
    std::string triple;
    friend bool operator==(const TargetPlatform &, const TargetPlatform &) = default;
  };
  Result<TargetPlatform> parse_target_platform(const std::string &value);

  struct DistributionArtifactDigest
  {
    std::string value;
    static Result<DistributionArtifactDigest> parse(const std::string &value);
    friend bool operator==(const DistributionArtifactDigest &, const DistributionArtifactDigest &) = default;
  };
  Result<DistributionArtifactDigest> digest_distribution_artifact(std::string_view exactArchiveBytes);
  Result<Unit> verify_distribution_artifact(
      std::string_view exactArchiveBytes, const DistributionArtifactDigest &expected);

  enum class ArchiveFormat { tar_gz, zip };
  enum class ComponentKind { host, target_sdk, platform_independent };

  struct ReleaseArtifact
  {
    std::string id;
    HostPlatform host;
    std::string location;
    ArchiveFormat archive{ArchiveFormat::tar_gz};
    DistributionArtifactDigest digest;
    std::optional<std::uint64_t> size;
  };

  struct ReleaseComponent
  {
    std::string id;
    ComponentKind kind{ComponentKind::platform_independent};
    std::optional<HostPlatform> host;
    std::optional<TargetPlatform> target;
    std::string location;
    ArchiveFormat archive{ArchiveFormat::tar_gz};
    DistributionArtifactDigest digest;
    std::optional<std::uint64_t> size;
    std::optional<std::string> installPath;
  };

  struct ReleaseManifest
  {
    std::int64_t format{1};
    std::string generation;
    vix::cli::project::native::Version version;
    std::string channel;
    std::uint64_t sequence{0};
    std::vector<ReleaseArtifact> artifacts;
    std::vector<ReleaseComponent> components;
  };

  struct SignatureEnvelope
  {
    std::string algorithm;
    std::string keyId;
    std::string signature;
  };
  Result<SignatureEnvelope> parse_signature_envelope(const std::string &text);
  Result<std::string> serialize_signature_envelope(const SignatureEnvelope &envelope);

  struct ReleaseTrustKey
  {
    std::string keyId;
    std::string algorithm;
    std::string publicKey;
  };

  struct ReleaseTrustStore
  {
    std::vector<ReleaseTrustKey> keys;
  };

  class VerifiedReleaseManifest
  {
  public:
    const ReleaseManifest &manifest() const noexcept { return manifest_; }

  private:
    explicit VerifiedReleaseManifest(ReleaseManifest manifest) : manifest_(std::move(manifest)) {}
    ReleaseManifest manifest_;
    friend struct detail::VerifiedReleaseManifestAccess;
  };

  Result<ReleaseManifest> parse_release_manifest(const std::string &text);
  Result<std::string> serialize_release_manifest(const ReleaseManifest &manifest);
  Result<std::string> release_signing_payload(const ReleaseManifest &manifest);
  Result<ReleaseTrustStore> parse_release_trust_store(const std::string &text);
  Result<ReleaseTrustStore> load_release_trust_store(const std::filesystem::path &path);
  Result<VerifiedReleaseManifest> verify_release_manifest(
      const std::string &manifestText,
      const SignatureEnvelope &signature,
      const ReleaseTrustStore &trustStore,
      const std::string &expectedGeneration = "current");

  enum class InstallKind { managed, archive, external };
  struct InstallReceipt
  {
    std::int64_t format{1};
    std::string generation;
    vix::cli::project::native::Version version;
    HostPlatform host;
    std::string channel;
    InstallKind installKind{InstallKind::managed};
    std::optional<std::string> externalOwner;
    std::string installationRoot;
  };
  Result<InstallReceipt> parse_install_receipt(const std::string &text);
  Result<std::string> serialize_install_receipt(const InstallReceipt &receipt);

  struct InstallationLayout
  {
    std::filesystem::path root;
    std::filesystem::path versions;
    std::filesystem::path versionRoot;
    std::filesystem::path bin;
    std::filesystem::path include;
    std::filesystem::path lib;
    std::filesystem::path sdk;
    std::filesystem::path share;
    std::filesystem::path current;
    std::filesystem::path receipt;
  };
  Result<InstallationLayout> installation_layout(
      const std::filesystem::path &root, const std::string &version);
  Result<std::filesystem::path> release_trust_store_path(const std::filesystem::path &installationRoot);
  Result<std::filesystem::path> distribution_home(
      const std::filesystem::path &parent, const std::string &generation);

  class InstallationActivator
  {
  public:
    virtual ~InstallationActivator() = default;
    virtual Result<std::optional<std::string>> current_version() = 0;
    virtual Result<Unit> activate(const std::string &version) = 0;
    virtual Result<Unit> rollback(const std::optional<std::string> &previousVersion) = 0;
  };

  enum class PlanKind
  {
    already_current,
    upgrade_available,
    wrong_generation,
    unsupported_host,
    externally_managed,
    archive_installation,
    channel_mismatch,
    no_newer_release
  };
  struct UpgradePlan
  {
    PlanKind kind{PlanKind::unsupported_host};
    vix::cli::project::native::Version from;
    vix::cli::project::native::Version to;
    std::optional<ReleaseArtifact> artifact;
    std::optional<std::string> externalOwner;
  };
  Result<UpgradePlan> plan_upgrade(
      const InstallReceipt &receipt,
      const VerifiedReleaseManifest &manifest,
      const HostPlatform &detectedHost);

  struct SignedReleaseManifest
  {
    std::string manifestText;
    SignatureEnvelope signature;
  };
  class ReleaseManifestSource
  {
  public:
    virtual ~ReleaseManifestSource() = default;
    virtual Result<SignedReleaseManifest> fetch(const std::string &channel) = 0;
  };
  class DistributionArtifactFetcher
  {
  public:
    virtual ~DistributionArtifactFetcher() = default;
    virtual Result<std::filesystem::path> fetch(
        const std::string &location, const std::filesystem::path &stagingPath) = 0;
  };

  class ArchiveExtractor
  {
  public:
    virtual ~ArchiveExtractor() = default;
    virtual Result<std::filesystem::path> extract(
        ArchiveFormat format, const std::filesystem::path &verifiedArchive,
        const std::filesystem::path &stagingDirectory) = 0;
  };
} // namespace vix::cli::distribution

#endif
