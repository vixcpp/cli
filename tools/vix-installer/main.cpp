#include <vix/cli/distribution/Runtime.hpp>
#include <vix_installer_provisioned.hpp>

#include <iostream>
#include <vector>

namespace
{
  std::vector<unsigned char> decode_hex(std::string_view text)
  {
    if (text.empty() || text.size() % 2 != 0)
      return {};
    const auto nibble = [](char value) -> int
    {
      if (value >= '0' && value <= '9') return value - '0';
      if (value >= 'a' && value <= 'f') return value - 'a' + 10;
      if (value >= 'A' && value <= 'F') return value - 'A' + 10;
      return -1;
    };
    std::vector<unsigned char> bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t index = 0; index < text.size(); index += 2)
    {
      const int high = nibble(text[index]);
      const int low = nibble(text[index + 1]);
      if (high < 0 || low < 0)
        return {};
      bytes.push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return bytes;
  }
}

int main(int argc, char **argv)
{
  using namespace vix::cli::distribution;
  const bool validateOnly = argc == 2 && std::string_view{argv[1]} == "--validate-provisioning";
  if (argc != 1 && !validateOnly)
  {
    std::cerr << "Usage: vix-installer [--validate-provisioning]\n";
    return 2;
  }
#ifdef _WIN32
  std::cerr << "vix-installer: managed installation is not supported on this platform yet.\n";
  return 1;
#else
  try
  {
    const auto sourceBytes = decode_hex(vix_installer::provisioned::distribution_source_hex);
    const auto trustBytes = decode_hex(vix_installer::provisioned::release_trust_hex);
    const std::string sourceText{sourceBytes.begin(), sourceBytes.end()};
    const std::string trustText{trustBytes.begin(), trustBytes.end()};
    const auto sourceConfig = parse_distribution_source_config(sourceText);
    if (!sourceConfig.valid())
    {
      std::cerr << "vix-installer: official distribution source was not provisioned: "
                << sourceConfig.error->message << '\n';
      return 1;
    }
    const auto trustStore = parse_release_trust_store(trustText);
    if (!trustStore.valid())
    {
      std::cerr << "vix-installer: trusted release authority was not provisioned: "
                << trustStore.error->message << '\n';
      return 1;
    }
    if (validateOnly)
    {
      std::cout << "Installer trust and stable source are provisioned.\n";
      return 0;
    }
    const auto host = detect_host_platform();
    const auto home = default_managed_vix_home();
    const auto exposed = default_vix_executable_path();
    if (!host.valid() || !home.valid() || !exposed.valid())
    {
      const auto &error = !host.valid() ? host.error.value() : !home.valid() ? home.error.value() : exposed.error.value();
      std::cerr << "vix-installer: " << error.message << '\n';
      return 1;
    }

    HttpsReleaseManifestSource source{*sourceConfig.value};
    HttpsDistributionArtifactFetcher fetcher;
    SafeTarGzExtractor extractor;
    PosixInstallationActivator activator{*home.value};
    PosixExecutableExposure pathExposure{*exposed.value};
    ManagedInstallerRuntime runtime{source, fetcher, extractor, activator, pathExposure};
    const auto result = runtime.install(*home.value, *trustStore.value, *exposed.value, *host.value, "stable");
    if (!result.valid())
    {
      std::cerr << "vix-installer: " << result.error->message << '\n';
      return 1;
    }
    std::cout << "Installed Vix " << result.value->version << " at " << result.value->installationRoot
              << "\nExecutable: " << result.value->exposedExecutable << '\n';
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "vix-installer: installation failed: " << error.what() << '\n';
    return 1;
  }
#endif
}
