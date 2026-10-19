#include <vix/cli/distribution/Distribution.hpp>
#include <toml.hpp>

#include <cstdlib>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string_view>
#include <vector>

namespace
{
  using namespace vix::cli::distribution;

  char *signing_key_environment_value() noexcept
  {
#if defined(_WIN32)
    char *value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "VIX_RELEASE_SIGNING_KEY_HEX") != 0)
      return nullptr;
    return value;
#else
    return std::getenv("VIX_RELEASE_SIGNING_KEY_HEX");
#endif
  }

  std::optional<std::string> copied_environment_value(const char *name)
  {
#if defined(_WIN32)
    char *value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
      return std::nullopt;
    std::string result{value};
    std::free(value);
#else
    const char *value = std::getenv(name);
    if (value == nullptr)
      return std::nullopt;
    std::string result{value};
#endif
    if (result.empty())
      return std::nullopt;
    return result;
  }

  void erase_signing_key_environment_value(char *value, std::size_t length) noexcept
  {
    if (value == nullptr)
      return;
    std::fill_n(value, length, '\0');
#if defined(_WIN32)
    (void)_putenv_s("VIX_RELEASE_SIGNING_KEY_HEX", "");
    std::free(value);
#endif
  }

  struct SigningKeyEnvironmentValue
  {
    char *value{};

    void clear() noexcept
    {
      if (value == nullptr)
        return;
      erase_signing_key_environment_value(value, std::char_traits<char>::length(value) + 1);
      value = nullptr;
    }

    ~SigningKeyEnvironmentValue() { clear(); }
  };

  std::optional<std::string> field(const toml::table &table, std::string_view key)
  {
    const auto value = table[key].value<std::string>();
    return value ? std::optional<std::string>{*value} : std::nullopt;
  }

  std::optional<std::uint64_t> unsigned_field(const toml::table &table, std::string_view key)
  {
    const auto value = table[key].value<std::int64_t>();
    if (!value || *value < 0)
      return std::nullopt;
    return static_cast<std::uint64_t>(*value);
  }

  std::optional<ArchiveFormat> archive_format(const std::optional<std::string> &value)
  {
    if (!value || *value == "tar.gz")
      return ArchiveFormat::tar_gz;
    if (*value == "zip")
      return ArchiveFormat::zip;
    return std::nullopt;
  }

  bool safe_token(std::string_view value)
  {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c)
    {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
  }

  bool safe_archive_reference(const std::string &file, const std::string &location)
  {
    const std::filesystem::path path{file};
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
        location.rfind("https://", 0) != 0)
      return false;
    for (const auto &part : path)
      if (part.empty() || part == "." || part == "..")
        return false;
    return true;
  }

  bool qualified_target(std::string_view target)
  {
    return target == "android-arm64-v8a-api21" || target == "android-x86_64-api21" ||
           target == "wasm32-wasip2";
  }

  std::optional<std::string> read_archive(const std::filesystem::path &path)
  {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    constexpr std::uintmax_t maximumBytes = 512ull * 1024ull * 1024ull;
    if (ec || size == 0 || size > maximumBytes)
      return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    if (!input)
      return std::nullopt;
    std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (bytes.size() != size)
      return std::nullopt;
    return bytes;
  }

  std::optional<ReleaseManifest> manifest_from_spec(const std::filesystem::path &specPath,
                                                     std::string &failure)
  {
    try
    {
      const auto spec = toml::parse_file(specPath.string());
      const auto versionText = field(spec, "version");
      const auto channel = field(spec, "channel");
      const auto sequence = unsigned_field(spec, "sequence");
      if (!versionText || !safe_token(*versionText) || !channel || !safe_token(*channel) || !sequence)
      {
        failure = "spec requires version, channel, and nonnegative sequence fields";
        return std::nullopt;
      }
      std::vector<ReleaseArtifact> artifacts;
      if (const auto *items = spec.get_as<toml::array>("artifact"))
      {
        for (const auto &node : *items)
        {
          const auto *table = node.as_table();
          if (!table)
          {
            failure = "artifact spec entries must be tables";
            return std::nullopt;
          }
          const auto id = field(*table, "id");
          const auto hostText = field(*table, "host");
          const auto location = field(*table, "location");
          const auto file = field(*table, "file");
          const auto archive = archive_format(field(*table, "archive"));
          if (!id || !safe_token(*id) || !hostText || !location || !file ||
              !safe_archive_reference(*file, *location) || !archive || *archive != ArchiveFormat::tar_gz)
          {
            failure = "artifact spec entry has missing or invalid fields";
            return std::nullopt;
          }
          const auto host = parse_host_platform(*hostText);
          const auto filePath = specPath.parent_path() / *file;
          const auto bytes = read_archive(filePath);
          const auto digest = bytes ? digest_distribution_artifact(*bytes)
                                    : Result<DistributionArtifactDigest>{};
          if (!host.valid() || !digest.valid())
          {
            failure = !host.valid() ? host.error->message : "artifact file is missing, empty, or exceeds 512 MiB";
            return std::nullopt;
          }
          artifacts.push_back({*id, *host.value, *location, *archive, *digest.value,
                               static_cast<std::uint64_t>(bytes->size())});
        }
      }
      std::vector<ReleaseComponent> components;
      if (const auto *items = spec.get_as<toml::array>("component"))
      {
        for (const auto &node : *items)
        {
          const auto *table = node.as_table();
          if (!table)
          {
            failure = "component spec entries must be tables";
            return std::nullopt;
          }
          const auto id = field(*table, "id");
          const auto kindText = field(*table, "kind");
          const auto targetText = field(*table, "target");
          const auto location = field(*table, "location");
          const auto file = field(*table, "file");
          const auto archive = archive_format(field(*table, "archive"));
          const auto installPath = field(*table, "install-path");
          if (!id || !safe_token(*id) || !kindText || *kindText != "target-sdk" ||
              !targetText || !qualified_target(*targetText) || !location || !file ||
              !safe_archive_reference(*file, *location) ||
              !archive || *archive != ArchiveFormat::tar_gz)
          {
            failure = "only complete target-sdk components are accepted by the release spec";
            return std::nullopt;
          }
          const auto target = parse_target_platform(*targetText);
          const auto filePath = specPath.parent_path() / *file;
          const auto bytes = read_archive(filePath);
          const auto digest = bytes ? digest_distribution_artifact(*bytes)
                                    : Result<DistributionArtifactDigest>{};
          if (!target.valid() || !digest.valid())
          {
            failure = !target.valid() ? target.error->message : "component file is missing, empty, or exceeds 512 MiB";
            return std::nullopt;
          }
          components.push_back({*id, ComponentKind::target_sdk, {}, *target.value, *location, *archive,
                                *digest.value, static_cast<std::uint64_t>(bytes->size()), installPath});
        }
      }
      if (artifacts.empty())
      {
        failure = "release spec must contain at least one host artifact";
        return std::nullopt;
      }
      const auto versionProbe = std::string{"format = 1\ngeneration = \"current\"\nversion = \""} + *versionText +
                                "\"\nchannel = \"stable\"\nsequence = 1\n\n[[artifact]]\nid = \"probe\"\nhost = \"" +
                                artifacts.front().host.canonical() + "\"\nlocation = \"https://example.invalid/artifact\"\narchive = \"tar.gz\"\ndigest = \"" +
                                artifacts.front().digest.value + "\"\n";
      const auto parsedVersion = parse_release_manifest(versionProbe);
      if (!parsedVersion.valid())
      {
        failure = "release spec version is invalid";
        return std::nullopt;
      }
      auto manifest = make_current_release_manifest(parsedVersion.value->version, *channel, *sequence,
                                                    std::move(artifacts), std::move(components));
      if (!manifest.valid())
      {
        failure = manifest.error->message;
        return std::nullopt;
      }
      return std::move(*manifest.value);
    }
    catch (const std::exception &error)
    {
      failure = error.what();
      return std::nullopt;
    }
  }

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
  const bool specMode = argc == 3 && std::string_view{argv[1]} == "--spec";
  if (!specMode && argc != 5)
  {
    std::cerr << "Usage: vix-release-tool <version> <host> <artifact-location> <archive>\n"
                 "       vix-release-tool --spec <release-spec.toml>\n";
    return 2;
  }
  const auto keyId = copied_environment_value("VIX_RELEASE_KEY_ID");
  SigningKeyEnvironmentValue mutableEnvironmentKey{signing_key_environment_value()};
  const char *keyHex = mutableEnvironmentKey.value;
  const auto trustPath = copied_environment_value("VIX_RELEASE_TRUST_FILE");
  if (!keyId || !keyHex || !*keyHex || !trustPath)
  {
    std::cerr << "vix-release-tool: release signing inputs or public trust verification input are not provisioned.\n";
    return 1;
  }
  try
  {
    Result<ReleaseManifest> manifestResult;
    if (specMode)
    {
      std::string failure;
      auto manifest = manifest_from_spec(argv[2], failure);
      if (!manifest)
      {
        std::cerr << "vix-release-tool: " << failure << '\n';
        return 1;
      }
      manifestResult = {std::move(*manifest), {}};
    }
    else
    {
      const auto bytes = read_archive(argv[4]);
      const auto host = parse_host_platform(argv[2]);
      const std::string versionInput{argv[1]};
      if (!bytes || !host.valid() || versionInput.empty() ||
          !std::all_of(versionInput.begin(), versionInput.end(), [](unsigned char c)
                       { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                                (c >= 'a' && c <= 'z') || c == '.' || c == '-'; }))
      {
        std::cerr << "vix-release-tool: version, host, or archive is invalid.\n";
        return 1;
      }
      const auto digest = digest_distribution_artifact(*bytes);
      const auto versionProbe = std::string{"format = 1\ngeneration = \"current\"\nversion = \""} + versionInput +
                                "\"\nchannel = \"stable\"\nsequence = 1\n\n[[artifact]]\nid = \"probe\"\nhost = \"" +
                                host.value->canonical() + "\"\nlocation = \"https://example.invalid/artifact\"\narchive = \"tar.gz\"\ndigest = \"" +
                                (digest.valid() ? digest.value->value : std::string{"sha256:"} + std::string(64, '0')) + "\"\n";
      const auto parsedVersion = parse_release_manifest(versionProbe);
      if (!parsedVersion.valid() || !digest.valid())
      {
        std::cerr << "vix-release-tool: version, host, or archive digest is invalid.\n";
        return 1;
      }
      ReleaseArtifact artifact{"current-" + host.value->canonical(), *host.value, argv[3],
                               ArchiveFormat::tar_gz, *digest.value,
                               static_cast<std::uint64_t>(bytes->size())};
      manifestResult = make_current_release_manifest(parsedVersion.value->version, "stable", 1,
                                                     {std::move(artifact)});
    }
    auto key = decode_hex(keyHex);
    mutableEnvironmentKey.clear();
    if (!manifestResult.valid() || key.size() != 64)
    {
      std::cerr << "vix-release-tool: manifest or signing key is invalid.\n";
      std::fill(key.begin(), key.end(), 0);
      return 1;
    }
    auto signature = sign_release_manifest(*manifestResult.value, *keyId, key);
    std::fill(key.begin(), key.end(), 0);
    if (!signature.valid())
    {
      std::cerr << "vix-release-tool: release signing failed: " << signature.error->message << '\n';
      return 1;
    }
    const auto manifestText = serialize_release_manifest(*manifestResult.value);
    const auto signatureText = serialize_signature_envelope(*signature.value);
    const auto trust = load_release_trust_store(*trustPath);
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
