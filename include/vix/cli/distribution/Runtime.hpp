#ifndef VIX_CLI_DISTRIBUTION_RUNTIME_HPP
#define VIX_CLI_DISTRIBUTION_RUNTIME_HPP

#include <vix/cli/distribution/Distribution.hpp>

#include <map>

namespace vix::cli::distribution
{
  inline constexpr std::uint64_t maximum_distribution_archive_bytes = 512ull * 1024ull * 1024ull;
  struct ChannelSource
  {
    std::string manifest;
    std::string signature;
  };

  struct DistributionSourceConfig
  {
    std::int64_t format{1};
    std::map<std::string, ChannelSource> channels;
  };

  Result<DistributionSourceConfig> parse_distribution_source_config(const std::string &text);
  Result<DistributionSourceConfig> load_distribution_source_config(const std::filesystem::path &path);
  Result<std::filesystem::path> distribution_source_config_path(const std::filesystem::path &versionRoot);

  class HttpsReleaseManifestSource final : public ReleaseManifestSource
  {
  public:
    explicit HttpsReleaseManifestSource(DistributionSourceConfig config) : config_(std::move(config)) {}
    Result<SignedReleaseManifest> fetch(const std::string &channel) override;

  private:
    DistributionSourceConfig config_;
  };

  class HttpsDistributionArtifactFetcher final : public DistributionArtifactFetcher
  {
  public:
    explicit HttpsDistributionArtifactFetcher(std::uint64_t maximumBytes = maximum_distribution_archive_bytes)
        : maximumBytes_(maximumBytes) {}
    Result<std::filesystem::path> fetch(const std::string &location, const std::filesystem::path &stagingPath) override;

  private:
    std::uint64_t maximumBytes_;
  };

  class PosixInstallationActivator final : public InstallationActivator
  {
  public:
    explicit PosixInstallationActivator(std::filesystem::path root) : root_(std::move(root)) {}
    Result<std::optional<std::string>> current_version() override;
    Result<Unit> activate(const std::string &version) override;
    Result<Unit> rollback(const std::optional<std::string> &previousVersion) override;

  private:
    std::filesystem::path root_;
  };

  enum class RuntimeOutcomeKind { already_current, upgraded, externally_managed, unmanaged_install };
  struct RuntimeOutcome
  {
    RuntimeOutcomeKind kind{RuntimeOutcomeKind::already_current};
    std::optional<std::string> externalOwner;
    std::optional<std::string> version;
  };

  class UpgradeRuntime
  {
  public:
    UpgradeRuntime(ReleaseManifestSource &source, DistributionArtifactFetcher &fetcher,
                   ArchiveExtractor &extractor, InstallationActivator &activator)
        : source_(source), fetcher_(fetcher), extractor_(extractor), activator_(activator) {}

    Result<RuntimeOutcome> run(const std::filesystem::path &receiptPath,
                               const std::filesystem::path &trustStorePath,
                               const HostPlatform &host);

  private:
    ReleaseManifestSource &source_;
    DistributionArtifactFetcher &fetcher_;
    ArchiveExtractor &extractor_;
    InstallationActivator &activator_;
  };

  class SafeTarGzExtractor final : public ArchiveExtractor
  {
  public:
    Result<std::filesystem::path> extract(ArchiveFormat format, const std::filesystem::path &verifiedArchive,
                                          const std::filesystem::path &stagingDirectory) override;
  };
} // namespace vix::cli::distribution

#endif
