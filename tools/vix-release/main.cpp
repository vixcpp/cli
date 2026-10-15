#include <vix/cli/distribution/Distribution.hpp>

#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace
{
  std::vector<std::uint8_t> decode_hex(const char *input)
  {
    if (!input)
      return {};
    const std::string text{input};
    if (text.empty() || text.size() % 2 != 0)
      return {};
    const auto nibble = [](char c) -> int
    {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2)
    {
      const int high = nibble(text[i]);
      const int low = nibble(text[i + 1]);
      if (high < 0 || low < 0)
        return {};
      bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return bytes;
  }

  bool write_text(const std::filesystem::path &path, const std::string &text)
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    return static_cast<bool>(output);
  }
}

int main(int argc, char **argv)
{
  using namespace vix::cli::distribution;
  if (argc != 5)
  {
    std::cerr << "Usage: vix-release-tool <version> <host> <artifact-location> <archive>\n";
    return 2;
  }
  const char *keyId = std::getenv("VIX_RELEASE_KEY_ID");
  const char *keyHex = std::getenv("VIX_RELEASE_SIGNING_KEY_HEX");
  const char *trustPath = std::getenv("VIX_RELEASE_TRUST_FILE");
  if (!keyId || !*keyId || !keyHex || !*keyHex || !trustPath || !*trustPath)
  {
    std::cerr << "vix-release-tool: release signing inputs or public trust verification input are not provisioned.\n";
    return 1;
  }
  const auto keyLength = std::char_traits<char>::length(keyHex);
  auto *mutableEnvironmentKey = std::getenv("VIX_RELEASE_SIGNING_KEY_HEX");
  try
  {
    std::error_code sizeError;
    const auto archiveSize = std::filesystem::file_size(argv[4], sizeError);
    constexpr std::uintmax_t maximumReleaseArtifactBytes = 512ull * 1024ull * 1024ull;
    if (sizeError || archiveSize == 0 || archiveSize > maximumReleaseArtifactBytes)
    {
      std::cerr << "vix-release-tool: release archive size is invalid or exceeds the supported bound.\n";
      return 1;
    }
    std::ifstream archiveInput(argv[4], std::ios::binary);
    if (!archiveInput)
    {
      std::cerr << "vix-release-tool: release archive could not be read.\n";
      return 1;
    }
    const std::string archiveBytes{std::istreambuf_iterator<char>{archiveInput}, std::istreambuf_iterator<char>{}};
    const auto host = parse_host_platform(argv[2]);
    const auto digest = digest_distribution_artifact(archiveBytes);
    const std::string versionInput{argv[1]};
    if (!host.valid() || versionInput.empty() ||
        !std::all_of(versionInput.begin(), versionInput.end(), [](unsigned char c)
                     { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                              (c >= 'a' && c <= 'z') || c == '.' || c == '-'; }))
    {
      std::cerr << "vix-release-tool: version or host is invalid.\n";
      return 1;
    }
    const auto versionProbe = std::string{"format = 1\ngeneration = \"current\"\nversion = \""} + versionInput +
                              "\"\nchannel = \"stable\"\nsequence = 1\n\n[[artifact]]\nid = \"probe\"\nhost = \"" +
                              host.value->canonical() + "\"\nlocation = \"https://example.invalid/artifact\"\narchive = \"tar.gz\"\ndigest = \"sha256:" +
                              (digest.valid() ? digest.value->value.substr(7) : std::string(64, '0')) + "\"\n";
    const auto parsedVersion = parse_release_manifest(versionProbe);
    if (!parsedVersion.valid() || !digest.valid())
    {
      std::cerr << "vix-release-tool: version, host, or artifact digest is invalid.\n";
      return 1;
    }
    ReleaseArtifact artifact{"current-" + host.value->canonical(), *host.value, argv[3],
                             ArchiveFormat::tar_gz, *digest.value,
                             static_cast<std::uint64_t>(archiveBytes.size())};
    auto manifest = make_current_release_manifest(parsedVersion.value->version, "stable", 1, {std::move(artifact)});
    auto key = decode_hex(keyHex);
    if (mutableEnvironmentKey)
      std::fill_n(mutableEnvironmentKey, keyLength, '\0');
    if (!manifest.valid() || key.size() != 64)
    {
      std::cerr << "vix-release-tool: manifest or signing key is invalid.\n";
      std::fill(key.begin(), key.end(), 0);
      return 1;
    }
    auto signature = sign_release_manifest(*manifest.value, keyId, key);
    std::fill(key.begin(), key.end(), 0);
    if (!signature.valid())
    {
      std::cerr << "vix-release-tool: release signing failed: " << signature.error->message << '\n';
      return 1;
    }
    const auto manifestText = serialize_release_manifest(*manifest.value);
    const auto signatureText = serialize_signature_envelope(*signature.value);
    const auto trust = load_release_trust_store(trustPath);
    const auto verified = manifestText.valid() && trust.valid()
                              ? verify_release_manifest(*manifestText.value, *signature.value, *trust.value, "current")
                              : Result<VerifiedReleaseManifest>{};
    if (!manifestText.valid() || !signatureText.valid() || !verified.valid() ||
        !write_text("release-manifest.toml", *manifestText.value) ||
        !write_text("release-signature.toml", *signatureText.value))
    {
      std::cerr << "vix-release-tool: signed release metadata could not be written.\n";
      return 1;
    }
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "vix-release-tool: release metadata generation failed: " << error.what() << '\n';
    return 1;
  }
}
