#include <vix/cli/commands/UpgradeCommand.hpp>
#include <vix/env/Legacy.hpp>

#include <vix/cli/distribution/Runtime.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace vix::commands
{
  namespace
  {
#ifndef _WIN32
    namespace fs = std::filesystem;
    using namespace vix::cli::distribution;

    std::string version_text(const vix::cli::project::native::Version &version)
    {
      auto value = std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
      if (version.prerelease)
        value += "-" + *version.prerelease;
      return value;
    }

    fs::path resolve_executable(const char *rawExecutable)
    {
      if (!rawExecutable || !*rawExecutable)
        return {};
      std::error_code ec;
      const fs::path requested{rawExecutable};
      if (requested.has_parent_path())
      {
        const auto resolved = fs::canonical(requested, ec);
        return ec ? fs::path{} : resolved;
      }
      const char *pathValue = vix::env::legacy::getenv("PATH");
      if (!pathValue)
        return {};
      std::string paths{pathValue};
      std::size_t offset = 0;
      while (offset <= paths.size())
      {
        const auto separator = paths.find(':', offset);
        const auto directory = paths.substr(offset, separator == std::string::npos ? std::string::npos : separator - offset);
        const auto candidate = fs::path{directory.empty() ? "." : directory} / requested;
        ec.clear();
        if (fs::is_regular_file(candidate, ec) && !ec)
        {
          const auto resolved = fs::canonical(candidate, ec);
          if (!ec)
            return resolved;
        }
        if (separator == std::string::npos)
          break;
        offset = separator + 1;
      }
      return {};
    }

    fs::path receipt_path_for_executable(const char *rawExecutable)
    {
      const auto executable = resolve_executable(rawExecutable);
      if (executable.empty())
        return {};
      std::error_code ec;
      auto candidate = executable.parent_path();
      while (!candidate.empty())
      {
        if (fs::is_regular_file(candidate / "install.toml", ec) && !ec)
          return candidate / "install.toml";
        ec.clear();
        const auto parent = candidate.parent_path();
        if (parent == candidate)
          break;
        candidate = parent;
      }
      if (const char *receipt = vix::env::legacy::getenv("VIX_INSTALL_RECEIPT"); receipt && *receipt && fs::is_regular_file(receipt))
        return fs::path{receipt};
      const auto systemPrefixReceipt = executable.parent_path().parent_path() / "share" / "vix" / "install.toml";
      if (fs::is_regular_file(systemPrefixReceipt))
        return systemPrefixReceipt;
      if (const char *xdg = vix::env::legacy::getenv("XDG_DATA_HOME"); xdg && *xdg)
      {
        const auto receipt = fs::path{xdg} / "vix" / "install.toml";
        if (fs::is_regular_file(receipt))
          return receipt;
      }
      if (const char *home = vix::env::legacy::getenv("HOME"); home && *home)
      {
        const auto receipt = fs::path{home} / ".local" / "share" / "vix" / "install.toml";
        if (fs::is_regular_file(receipt))
          return receipt;
      }
      return {};
    }
#endif
  }

  int UpgradeCommand::run(const std::vector<std::string> &args)
  {
    if (!args.empty())
    {
      std::cerr << "Usage: vix upgrade\n";
      return 2;
    }
#ifdef _WIN32
    std::cerr << "vix: managed self-upgrade is not supported on this platform yet.\n";
    return 1;
#else
    const auto receiptPath = receipt_path_for_executable(vix::env::legacy::getenv("VIX_CLI_PATH"));
    if (receiptPath.empty())
    {
      std::cerr << "vix: installation receipt is unavailable; reinstall through a managed distribution before using upgrade.\n";
      return 1;
    }
    std::ifstream receiptInput(receiptPath, std::ios::binary);
    const std::string receiptText{std::istreambuf_iterator<char>{receiptInput}, std::istreambuf_iterator<char>{}};
    const auto receipt = parse_install_receipt(receiptText);
    if (!receipt.valid())
    {
      std::cerr << "vix: upgrade failed: " << receipt.error->message << '\n';
      return 1;
    }
    const fs::path root{receipt.value->installationRoot};
    const auto previousVersion = version_text(receipt.value->version);
    if (root.empty())
    {
      std::cerr << "vix: upgrade failed: installation receipt has no installation root.\n";
      return 1;
    }
    if (receipt.value->installKind == InstallKind::managed)
    {
      std::error_code ec;
      const auto runningExecutable = resolve_executable(vix::env::legacy::getenv("VIX_CLI_PATH"));
      if (runningExecutable.empty())
      {
        std::cerr << "vix: cannot identify the running managed executable.\n";
        return 1;
      }
      const auto activeExecutable = fs::canonical(root / "current" / "bin" / "vix", ec);
      if (ec || runningExecutable != activeExecutable)
      {
        std::cerr << "vix: installation receipt does not match the active managed executable.\n";
        return 1;
      }
    }

    const auto configPath = distribution_source_config_path(root / "current");
    if (!configPath.valid())
    {
      std::cerr << "vix: " << configPath.error->message << '\n';
      return 1;
    }
    const auto config = load_distribution_source_config(*configPath.value);
    if (receipt.value->installKind == InstallKind::managed && !config.valid())
    {
      std::cerr << "vix: upgrade failed: " << config.error->message << '\n';
      return 1;
    }
    DistributionSourceConfig sourceConfig;
    if (config.valid())
      sourceConfig = *config.value;
    HttpsReleaseManifestSource source{std::move(sourceConfig)};
    HttpsDistributionArtifactFetcher fetcher;
    SafeTarGzExtractor extractor;
    PosixInstallationActivator activator{root};
    UpgradeRuntime runtime{source, fetcher, extractor, activator};
    const auto trustPath = root / "current" / "share" / "vix" / "trust" / "releases.toml";
    const auto host = detect_host_platform();
    if (!host.valid())
    {
      std::cerr << "vix: upgrade failed: " << host.error->message << '\n';
      return 1;
    }
    const auto result = runtime.run(receiptPath, trustPath, *host.value);
    if (!result.valid())
    {
      std::cerr << "vix: upgrade failed: " << result.error->message << '\n';
      return 1;
    }
    switch (result.value->kind)
    {
    case RuntimeOutcomeKind::already_current:
      std::cout << "Vix " << result.value->version.value_or("current") << " is already up to date.\n";
      return 0;
    case RuntimeOutcomeKind::upgraded:
      std::cout << "Vix upgraded from " << previousVersion << " to "
                << result.value->version.value_or("the new version") << ".\n";
      return 0;
    case RuntimeOutcomeKind::externally_managed:
      std::cerr << "This Vix installation is managed externally"
                << (result.value->externalOwner ? " by " + *result.value->externalOwner : std::string{})
                << ". Use that package manager to upgrade it.\n";
      return 1;
    case RuntimeOutcomeKind::unmanaged_install:
      std::cerr << "This Vix installation is not managed; replace it using its original installation method.\n";
      return 1;
    }
    return 1;
#endif
  }

  int UpgradeCommand::help()
  {
    std::cout << "Usage:\n  vix upgrade\n\nUpgrade a managed current-Vix installation.\n";
    return 0;
  }
}
