#include <vix/cli/distribution/Distribution.hpp>

#include <toml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <string_view>

#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/Validation.hpp>
#include <vix/cli/util/Semver.hpp>
#include <vix/crypto/signature.hpp>

#if defined(__linux__)
#include <features.h>
#endif

namespace vix::cli::distribution
{
  namespace
  {
    using Table = toml::table;

    Error error(ErrorCode code, std::string message)
    {
      return Error{code, std::move(message)};
    }

    bool lower_hex(std::string_view value)
    {
      return std::all_of(value.begin(), value.end(), [](char c)
                         { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }

    std::optional<std::vector<std::uint8_t>> decode_hex(std::string_view value)
    {
      if ((value.size() % 2) != 0 || !lower_hex(value))
        return std::nullopt;
      std::vector<std::uint8_t> bytes(value.size() / 2);
      for (std::size_t i = 0; i < bytes.size(); ++i)
      {
        const auto digit = [](char c) -> unsigned
        { return c <= '9' ? unsigned(c - '0') : unsigned(c - 'a' + 10); };
        bytes[i] = static_cast<std::uint8_t>((digit(value[i * 2]) << 4) | digit(value[i * 2 + 1]));
      }
      return bytes;
    }

    std::string quote(std::string_view value)
    {
      std::string out{"\""};
      for (const char c : value)
      {
        switch (static_cast<unsigned char>(c))
        {
        case '\\':
          out += "\\\\";
          break;
        case '"':
          out += "\\\"";
          break;
        case '\b':
          out += "\\b";
          break;
        case '\t':
          out += "\\t";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\f':
          out += "\\f";
          break;
        case '\r':
          out += "\\r";
          break;
        default:
          if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) == 0x7f)
          {
            static constexpr char hex[] = "0123456789abcdef";
            const auto byte = static_cast<unsigned char>(c);
            out += "\\u00";
            out.push_back(hex[byte >> 4]);
            out.push_back(hex[byte & 0x0f]);
          }
          else
            out.push_back(c);
        }
      }
      out.push_back('"');
      return out;
    }

    std::optional<std::string> text_field(const Table &table, std::string_view key)
    {
      const auto *node = table.get(key);
      if (!node)
        return std::nullopt;
      const auto value = node->value<std::string>();
      if (!value)
        return std::nullopt;
      return *value;
    }

    std::optional<std::uint64_t> uint_field(const Table &table, std::string_view key)
    {
      const auto *node = table.get(key);
      if (!node)
        return std::nullopt;
      if (const auto value = node->value<std::int64_t>(); value && *value >= 0)
        return static_cast<std::uint64_t>(*value);
      return std::nullopt;
    }

    bool only_keys(const Table &table, std::initializer_list<std::string_view> allowed)
    {
      for (const auto &entry : table)
      {
        const auto name = entry.first.str();
        if (std::find(allowed.begin(), allowed.end(), std::string_view{name}) == allowed.end())
          return false;
      }
      return true;
    }

    std::optional<vix::cli::project::native::Version> parse_version(std::string_view value)
    {
      const auto dash = value.find('-');
      const auto core = value.substr(0, dash);
      std::array<std::uint64_t, 3> parts{};
      std::size_t begin = 0;
      for (std::size_t i = 0; i < parts.size(); ++i)
      {
        const auto end = i == 2 ? core.size() : core.find('.', begin);
        if (end == std::string_view::npos || end <= begin || (end - begin > 1 && core[begin] == '0'))
          return std::nullopt;
        const auto parsed = std::from_chars(core.data() + begin, core.data() + end, parts[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != core.data() + end)
          return std::nullopt;
        begin = end + 1;
      }
      if (begin != core.size() + 1)
        return std::nullopt;
      if (dash == std::string_view::npos)
      {
        vix::cli::project::native::Version version{parts[0], parts[1], parts[2], {}};
        if (!vix::cli::project::native::valid_semver(version))
          return std::nullopt;
        return version;
      }
      const auto pre = value.substr(dash + 1);
      if (pre.empty() || !std::all_of(pre.begin(), pre.end(), [](unsigned char c)
                                      { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'; }))
        return std::nullopt;
      vix::cli::project::native::Version version{parts[0], parts[1], parts[2], std::string{pre}};
      if (!vix::cli::project::native::valid_semver(version))
        return std::nullopt;
      return version;
    }

    std::string version_text(const vix::cli::project::native::Version &version)
    {
      auto value = std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
      if (version.prerelease)
        value += "-" + *version.prerelease;
      return value;
    }

    std::string archive_name(ArchiveFormat value)
    {
      return value == ArchiveFormat::zip ? "zip" : "tar.gz";
    }

    std::optional<ArchiveFormat> parse_archive(std::string_view value)
    {
      if (value == "zip")
        return ArchiveFormat::zip;
      if (value == "tar.gz")
        return ArchiveFormat::tar_gz;
      return std::nullopt;
    }

    std::string component_kind_name(ComponentKind value)
    {
      switch (value)
      {
      case ComponentKind::host:
        return "host";
      case ComponentKind::target_sdk:
        return "target-sdk";
      case ComponentKind::platform_independent:
        return "platform-independent";
      }
      return "platform-independent";
    }

    std::optional<ComponentKind> parse_component_kind(std::string_view value)
    {
      if (value == "host")
        return ComponentKind::host;
      if (value == "target-sdk")
        return ComponentKind::target_sdk;
      if (value == "platform-independent")
        return ComponentKind::platform_independent;
      return std::nullopt;
    }

    std::string install_kind_name(InstallKind value)
    {
      switch (value)
      {
      case InstallKind::managed:
        return "managed";
      case InstallKind::archive:
        return "archive";
      case InstallKind::external:
        return "external";
      }
      return "managed";
    }

    std::optional<InstallKind> parse_install_kind(std::string_view value)
    {
      if (value == "managed")
        return InstallKind::managed;
      if (value == "archive")
        return InstallKind::archive;
      if (value == "external")
        return InstallKind::external;
      return std::nullopt;
    }

    bool valid_id(std::string_view value)
    {
      return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c)
                                           { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'; });
    }

    bool safe_relative_asset_path(std::string_view value)
    {
      if (value.empty() || value.front() == '/' || value.back() == '/' || value.find('\\') != std::string_view::npos || value.find(':') != std::string_view::npos)
        return false;
      std::size_t begin = 0;
      while (begin < value.size())
      {
        const auto end = value.find('/', begin);
        const auto part = value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        if (part.empty() || part == "." || part == "..")
          return false;
        if (end == std::string_view::npos)
          break;
        begin = end + 1;
      }
      return true;
    }

    Result<ReleaseArtifact> parse_artifact(const Table &table)
    {
      if (!only_keys(table, {"id", "host", "location", "archive", "digest", "size"}))
        return {{}, error(ErrorCode::malformed_manifest, "release artifact contains an unsupported field")};
      const auto id = text_field(table, "id");
      const auto hostText = text_field(table, "host");
      const auto location = text_field(table, "location");
      const auto archiveText = text_field(table, "archive");
      const auto digestText = text_field(table, "digest");
      if (!id || !valid_id(*id) || !hostText || !location || location->empty() || !archiveText || !digestText)
        return {{}, error(ErrorCode::malformed_manifest, "release artifact has missing or invalid fields")};
      auto host = parse_host_platform(*hostText);
      auto archive = parse_archive(*archiveText);
      auto digest = DistributionArtifactDigest::parse(*digestText);
      if (!host.valid())
        return {{}, host.error};
      if (!archive)
        return {{}, error(ErrorCode::malformed_manifest, "release artifact archive format is unsupported")};
      if (!digest.valid())
        return {{}, digest.error};
      ReleaseArtifact result{*id, *host.value, *location, *archive, *digest.value, std::nullopt};
      if (table.contains("size"))
      {
        const auto size = uint_field(table, "size");
        if (!size)
          return {{}, error(ErrorCode::malformed_manifest, "release artifact size is invalid")};
        result.size = *size;
      }
      return {std::move(result), {}};
    }
  } // namespace

  namespace detail
  {
    struct VerifiedReleaseManifestAccess
    {
      static VerifiedReleaseManifest make(ReleaseManifest manifest)
      {
        return VerifiedReleaseManifest{std::move(manifest)};
      }
    };
  } // namespace detail

  std::string HostPlatform::canonical() const
  {
    auto value = architecture + "-" + operatingSystem;
    if (!abi.empty())
      value += "-" + abi;
    return value;
  }

  Result<HostPlatform> parse_host_platform(const std::string &value)
  {
    static const std::array<HostPlatform, 6> supported{{{"x86_64", "linux", "gnu"}, {"aarch64", "linux", "gnu"}, {"x86_64", "windows", "msvc"}, {"aarch64", "windows", "msvc"}, {"x86_64", "macos", ""}, {"aarch64", "macos", ""}}};
    const auto it = std::find_if(supported.begin(), supported.end(), [&](const auto &host)
                                 { return host.canonical() == value; });
    if (it == supported.end())
      return {{}, error(ErrorCode::unsupported_host, "unsupported host platform tuple: " + value)};
    return {*it, {}};
  }

  Result<HostPlatform> detect_host_platform()
  {
    std::string architecture;
#if defined(__x86_64__) || defined(_M_X64)
    architecture = "x86_64";
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    architecture = "aarch64";
#else
    return {{}, error(ErrorCode::unsupported_host, "host architecture is not supported by this Vix distribution")};
#endif
#if defined(_WIN32)
#if defined(_MSC_VER)
    return parse_host_platform(architecture + "-windows-msvc");
#else
    return {{}, error(ErrorCode::unsupported_host, "Windows ABI is not MSVC and cannot be identified as a supported host")};
#endif
#elif defined(__APPLE__)
    return parse_host_platform(architecture + "-macos");
#elif defined(__linux__)
#if defined(__GLIBC__)
    return parse_host_platform(architecture + "-linux-gnu");
#else
    return {{}, error(ErrorCode::unsupported_host, "Linux libc ABI is not reliably identifiable as GNU at build time")};
#endif
#else
    return {{}, error(ErrorCode::unsupported_host, "operating system is not supported by this Vix distribution")};
#endif
  }

  Result<TargetPlatform> parse_target_platform(const std::string &value)
  {
    if (!valid_id(value) || value.find('-') == std::string::npos)
      return {{}, error(ErrorCode::invalid_value, "invalid target platform triple")};
    return {TargetPlatform{value}, {}};
  }

  Result<DistributionArtifactDigest> DistributionArtifactDigest::parse(const std::string &value)
  {
    if (value.size() != 71 || value.compare(0, 7, "sha256:") != 0 || !lower_hex(std::string_view{value}.substr(7)))
      return {{}, error(ErrorCode::invalid_artifact_digest, "distribution artifact digest must be sha256 followed by 64 lowercase hex characters")};
    return {DistributionArtifactDigest{value}, {}};
  }

  Result<DistributionArtifactDigest> digest_distribution_artifact(std::string_view exactArchiveBytes)
  {
    return DistributionArtifactDigest::parse(vix::cli::project::native::sha256_digest(exactArchiveBytes));
  }

  Result<Unit> verify_distribution_artifact(std::string_view exactArchiveBytes, const DistributionArtifactDigest &expected)
  {
    const auto actual = digest_distribution_artifact(exactArchiveBytes);
    if (!actual.valid() || actual.value->value != expected.value)
      return {{}, error(ErrorCode::invalid_artifact_digest, "distribution archive bytes do not match the authenticated digest")};
    return {Unit{}, {}};
  }

  Result<SignatureEnvelope> parse_signature_envelope(const std::string &text)
  {
    try
    {
      const auto root = toml::parse(text);
      if (!only_keys(root, {"format", "algorithm", "key-id", "signature"}))
        return {{}, error(ErrorCode::invalid_signature, "release signature envelope contains an unsupported field")};
      const auto format = root["format"].value<std::int64_t>();
      const auto algorithm = root["algorithm"].value<std::string>();
      const auto keyId = root["key-id"].value<std::string>();
      const auto signature = root["signature"].value<std::string>();
      if (!format || *format != 1 || !algorithm || !keyId || keyId->empty() || !signature)
        return {{}, error(ErrorCode::invalid_signature, "release signature envelope is malformed")};
      return {SignatureEnvelope{*algorithm, *keyId, *signature}, {}};
    }
    catch (const std::exception &e)
    {
      return {{}, error(ErrorCode::invalid_signature, e.what())};
    }
  }

  Result<std::string> serialize_signature_envelope(const SignatureEnvelope &envelope)
  {
    if (envelope.algorithm.empty() || envelope.keyId.empty() || envelope.signature.empty())
      return {{}, error(ErrorCode::invalid_signature, "release signature envelope is incomplete")};
    return {"format = 1\nalgorithm = " + quote(envelope.algorithm) + "\nkey-id = " + quote(envelope.keyId) +
                "\nsignature = " + quote(envelope.signature) + "\n",
            {}};
  }

  Result<ReleaseManifest> parse_release_manifest(const std::string &text)
  {
    try
    {
      const auto root = toml::parse(text);
      if (!only_keys(root, {"format", "generation", "version", "channel", "sequence", "artifact", "component"}))
        return {{}, error(ErrorCode::malformed_manifest, "release manifest contains an unsupported field")};
      const auto format = root["format"].value<std::int64_t>();
      const auto generation = root["generation"].value<std::string>();
      const auto versionText = root["version"].value<std::string>();
      const auto version = versionText ? parse_version(*versionText) : std::nullopt;
      const auto channel = root["channel"].value<std::string>();
      const auto sequence = root["sequence"].value<std::int64_t>();
      if (!format || *format != 1 || !generation || generation->empty() || !version || !channel || channel->empty() || !sequence || *sequence < 0)
        return {{}, error(ErrorCode::malformed_manifest, "release manifest format or required fields are invalid")};
      ReleaseManifest result{*format, *generation, *version, *channel, static_cast<std::uint64_t>(*sequence), {}, {}};
      std::set<std::string> artifactIds;
      std::set<std::string> artifactHosts;
      if (root.contains("artifact") && !root.get_as<toml::array>("artifact"))
        return {{}, error(ErrorCode::malformed_manifest, "artifact must be an array of tables")};
      if (root.contains("component") && !root.get_as<toml::array>("component"))
        return {{}, error(ErrorCode::malformed_manifest, "component must be an array of tables")};
      if (const auto *items = root.get_as<toml::array>("artifact"))
      {
        for (const auto &node : *items)
        {
          const auto *table = node.as_table();
          if (!table)
            return {{}, error(ErrorCode::malformed_manifest, "artifact entry must be a table")};
          auto artifact = parse_artifact(*table);
          if (!artifact.valid())
            return {{}, artifact.error};
          if (!artifactIds.insert(artifact.value->id).second)
            return {{}, error(ErrorCode::malformed_manifest, "duplicate artifact id")};
          if (!artifactHosts.insert(artifact.value->host.canonical()).second)
            return {{}, error(ErrorCode::malformed_manifest, "multiple release artifacts claim the same host tuple")};
          result.artifacts.push_back(std::move(*artifact.value));
        }
      }
      std::set<std::string> componentIds;
      if (const auto *items = root.get_as<toml::array>("component"))
      {
        for (const auto &node : *items)
        {
          const auto *table = node.as_table();
          if (!table)
            return {{}, error(ErrorCode::malformed_manifest, "component entry must be a table")};
          if (!only_keys(*table, {"id", "kind", "host", "target", "location", "archive", "digest", "size", "install-path"}))
            return {{}, error(ErrorCode::malformed_manifest, "release component contains an unsupported field")};
          const auto id = text_field(*table, "id");
          const auto kindText = text_field(*table, "kind");
          const auto location = text_field(*table, "location");
          const auto archiveText = text_field(*table, "archive");
          const auto digestText = text_field(*table, "digest");
          const auto kind = kindText ? parse_component_kind(*kindText) : std::nullopt;
          const auto archive = archiveText ? parse_archive(*archiveText) : std::nullopt;
          const auto digest = digestText ? DistributionArtifactDigest::parse(*digestText) : Result<DistributionArtifactDigest>{};
          if (!id || !valid_id(*id) || !componentIds.insert(*id).second || !kind || !location || location->empty() || !archive || !digest.valid())
            return {{}, error(digest.error ? ErrorCode::invalid_artifact_digest : ErrorCode::malformed_manifest, "release component has missing or invalid fields")};
          ReleaseComponent component{*id, *kind, {}, {}, *location, *archive, *digest.value, {}, {}};
          if (*kind == ComponentKind::host)
          {
            if (table->contains("target"))
              return {{}, error(ErrorCode::malformed_manifest, "host component cannot declare a target tuple")};
            const auto hostText = text_field(*table, "host");
            if (!hostText)
              return {{}, error(ErrorCode::malformed_manifest, "host component is missing host tuple")};
            auto host = parse_host_platform(*hostText);
            if (!host.valid())
              return {{}, host.error};
            component.host = *host.value;
          }
          else if (*kind == ComponentKind::target_sdk)
          {
            if (table->contains("host"))
              return {{}, error(ErrorCode::malformed_manifest, "target SDK cannot declare a host tuple")};
            const auto targetText = text_field(*table, "target");
            if (!targetText)
              return {{}, error(ErrorCode::malformed_manifest, "target SDK is missing target tuple")};
            auto target = parse_target_platform(*targetText);
            if (!target.valid())
              return {{}, target.error};
            component.target = *target.value;
          }
          else if (table->contains("host") || table->contains("target"))
            return {{}, error(ErrorCode::malformed_manifest, "platform-independent component cannot declare a platform tuple")};
          if (table->contains("size"))
          {
            const auto size = uint_field(*table, "size");
            if (!size)
              return {{}, error(ErrorCode::malformed_manifest, "component size is invalid")};
            component.size = *size;
          }
          if (const auto path = text_field(*table, "install-path"))
          {
            if (!safe_relative_asset_path(*path))
              return {{}, error(ErrorCode::malformed_manifest, "component install-path must be a safe relative path")};
            component.installPath = *path;
          }
          else if (table->contains("install-path"))
            return {{}, error(ErrorCode::malformed_manifest, "component install-path must be a string")};
          result.components.push_back(std::move(component));
        }
      }
      if (result.artifacts.empty())
        return {{}, error(ErrorCode::malformed_manifest, "release manifest must contain at least one host artifact")};
      return {std::move(result), {}};
    }
    catch (const std::exception &e)
    {
      return {{}, error(ErrorCode::malformed_manifest, e.what())};
    }
  }

  Result<std::string> serialize_release_manifest(const ReleaseManifest &manifest)
  {
    if (manifest.format != 1 || manifest.generation.empty() || !parse_version(version_text(manifest.version)) || manifest.channel.empty() || manifest.artifacts.empty() ||
        manifest.sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
      return {{}, error(ErrorCode::malformed_manifest, "release manifest is not valid format 1")};
    std::ostringstream out;
    out << "format = 1\ngeneration = " << quote(manifest.generation)
        << "\nversion = " << quote(version_text(manifest.version)) << "\nchannel = " << quote(manifest.channel)
        << "\nsequence = " << manifest.sequence << "\n";
    auto artifacts = manifest.artifacts;
    std::sort(artifacts.begin(), artifacts.end(), [](const auto &a, const auto &b)
              { return a.id < b.id; });
    std::set<std::string> ids;
    std::set<std::string> hosts;
    for (const auto &a : artifacts)
    {
      const auto host = parse_host_platform(a.host.canonical());
      if (!host.valid())
        return {{}, host.error};
      const auto digest = DistributionArtifactDigest::parse(a.digest.value);
      if (!digest.valid())
        return {{}, digest.error};
      if (!valid_id(a.id) || !ids.insert(a.id).second || a.location.empty() ||
          !hosts.insert(a.host.canonical()).second || (a.size && *a.size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))
        return {{}, error(ErrorCode::malformed_manifest, "release artifact is invalid or duplicated")};
      out << "\n[[artifact]]\nid = " << quote(a.id) << "\nhost = " << quote(a.host.canonical())
          << "\nlocation = " << quote(a.location) << "\narchive = " << quote(archive_name(a.archive))
          << "\ndigest = " << quote(a.digest.value) << "\n";
      if (a.size)
        out << "size = " << *a.size << "\n";
    }
    auto components = manifest.components;
    std::sort(components.begin(), components.end(), [](const auto &a, const auto &b)
              { return a.id < b.id; });
    ids.clear();
    for (const auto &c : components)
    {
      const auto digest = DistributionArtifactDigest::parse(c.digest.value);
      const bool platformValid =
          (c.kind == ComponentKind::host && c.host && !c.target && parse_host_platform(c.host->canonical()).valid()) ||
          (c.kind == ComponentKind::target_sdk && c.target && !c.host && parse_target_platform(c.target->triple).valid()) ||
          (c.kind == ComponentKind::platform_independent && !c.host && !c.target);
      if (!digest.valid())
        return {{}, digest.error};
      if (!valid_id(c.id) || !ids.insert(c.id).second || c.location.empty() || !platformValid ||
          (c.size && *c.size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) ||
          (c.installPath && !safe_relative_asset_path(*c.installPath)))
        return {{}, error(ErrorCode::malformed_manifest, "release component is invalid or duplicated")};
      out << "\n[[component]]\nid = " << quote(c.id) << "\nkind = " << quote(component_kind_name(c.kind)) << "\n";
      if (c.host)
        out << "host = " << quote(c.host->canonical()) << "\n";
      if (c.target)
        out << "target = " << quote(c.target->triple) << "\n";
      out << "location = " << quote(c.location) << "\narchive = " << quote(archive_name(c.archive))
          << "\ndigest = " << quote(c.digest.value) << "\n";
      if (c.size)
        out << "size = " << *c.size << "\n";
      if (c.installPath)
        out << "install-path = " << quote(*c.installPath) << "\n";
    }
    return {out.str(), {}};
  }

  Result<std::string> release_signing_payload(const ReleaseManifest &manifest)
  {
    auto serialized = serialize_release_manifest(manifest);
    if (!serialized.valid())
      return {{}, serialized.error};
    return {"VIX-RELEASE-MANIFEST-v1\n" + *serialized.value, {}};
  }

  Result<ReleaseTrustStore> parse_release_trust_store(const std::string &text)
  {
    try
    {
      const auto root = toml::parse(text);
      if (!only_keys(root, {"format", "trusted-key"}))
        return {{}, error(ErrorCode::invalid_value, "release trust store contains an unsupported field")};
      const auto format = root["format"].value<std::int64_t>();
      if (!format || *format != 1)
        return {{}, error(ErrorCode::invalid_value, "release trust store format must be 1")};
      ReleaseTrustStore store;
      std::set<std::string> ids;
      if (root.contains("trusted-key") && !root.get_as<toml::array>("trusted-key"))
        return {{}, error(ErrorCode::invalid_value, "trusted-key must be an array of tables")};
      if (const auto *items = root.get_as<toml::array>("trusted-key"))
      {
        for (const auto &node : *items)
        {
          const auto *table = node.as_table();
          if (!table)
            return {{}, error(ErrorCode::invalid_value, "release trust key must be a table")};
          if (!only_keys(*table, {"key-id", "algorithm", "public-key"}))
            return {{}, error(ErrorCode::invalid_value, "release trust key contains an unsupported field")};
          const auto id = text_field(*table, "key-id");
          const auto algorithm = text_field(*table, "algorithm");
          const auto key = text_field(*table, "public-key");
          if (!id || id->empty() || !ids.insert(*id).second || !algorithm || !key || key->size() != 64 || !lower_hex(*key))
            return {{}, error(ErrorCode::invalid_value, "release trust key is invalid or duplicated")};
          store.keys.push_back({*id, *algorithm, *key});
        }
      }
      if (store.keys.empty())
        return {{}, error(ErrorCode::invalid_value, "release trust store contains no trusted public keys")};
      return {std::move(store), {}};
    }
    catch (const std::exception &e)
    {
      return {{}, error(ErrorCode::invalid_value, e.what())};
    }
  }

  Result<ReleaseTrustStore> load_release_trust_store(const std::filesystem::path &path)
  {
    if (path.empty())
      return {{}, error(ErrorCode::invalid_value, "release trust store path is empty")};
    std::ifstream input(path, std::ios::binary);
    if (!input)
      return {{}, error(ErrorCode::invalid_value, "installed release trust store is unavailable")};
    const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input.good() && !input.eof())
      return {{}, error(ErrorCode::invalid_value, "installed release trust store could not be read")};
    return parse_release_trust_store(text);
  }

  Result<VerifiedReleaseManifest> verify_release_manifest(const std::string &manifestText, const SignatureEnvelope &signature,
                                                          const ReleaseTrustStore &trustStore, const std::string &expectedGeneration)
  {
    auto parsed = parse_release_manifest(manifestText);
    if (!parsed.valid())
      return {{}, parsed.error};
    const auto key = std::find_if(trustStore.keys.begin(), trustStore.keys.end(), [&](const auto &item)
                                  { return item.keyId == signature.keyId; });
    if (key == trustStore.keys.end())
      return {{}, error(ErrorCode::unknown_key, "release signing key is not trusted")};
    if (signature.algorithm != "ed25519" || key->algorithm != "ed25519")
      return {{}, error(ErrorCode::unsupported_algorithm, "release signature algorithm is not supported")};
    const auto publicKey = decode_hex(key->publicKey);
    const auto signatureBytes = decode_hex(signature.signature);
    if (!publicKey || publicKey->size() != 32 || !signatureBytes || signatureBytes->size() != 64)
      return {{}, error(ErrorCode::invalid_signature, "release signature envelope is malformed")};
    auto payload = release_signing_payload(*parsed.value);
    if (!payload.valid())
      return {{}, payload.error};
    const auto message = std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t *>(payload.value->data()), payload.value->size()};
    const auto keyBytes = std::span<const std::uint8_t>{publicKey->data(), publicKey->size()};
    const auto sigBytes = std::span<const std::uint8_t>{signatureBytes->data(), signatureBytes->size()};
    if (!vix::crypto::verify(vix::crypto::SignatureAlg::ed25519, keyBytes, message, sigBytes))
      return {{}, error(ErrorCode::invalid_signature, "release manifest signature verification failed")};
    if (parsed.value->generation != expectedGeneration)
      return {{}, error(ErrorCode::wrong_generation, "release manifest generation does not match this Vix installation")};
    return {detail::VerifiedReleaseManifestAccess::make(std::move(*parsed.value)), {}};
  }

  Result<InstallReceipt> parse_install_receipt(const std::string &text)
  {
    try
    {
      const auto root = toml::parse(text);
      if (!only_keys(root, {"format", "generation", "version", "host", "channel", "install-kind", "external-owner", "installation-root"}))
        return {{}, error(ErrorCode::invalid_receipt, "installation receipt contains an unsupported field")};
      const auto format = root["format"].value<std::int64_t>();
      const auto generation = root["generation"].value<std::string>();
      const auto versionText = root["version"].value<std::string>();
      const auto version = versionText ? parse_version(*versionText) : std::nullopt;
      const auto hostText = root["host"].value<std::string>();
      const auto channel = root["channel"].value<std::string>();
      const auto kindText = root["install-kind"].value<std::string>();
      const auto installRoot = root["installation-root"].value<std::string>();
      const auto owner = root["external-owner"].value<std::string>();
      if (!format || *format != 1 || !generation || generation->empty() || !version || !hostText || !channel || channel->empty() || !kindText || !installRoot)
        return {{}, error(ErrorCode::invalid_receipt, "installation receipt fields are invalid")};
      auto host = parse_host_platform(*hostText);
      auto kind = parse_install_kind(*kindText);
      if (!host.valid() || !kind || installRoot->empty() || (*kind == InstallKind::external && (!owner || owner->empty())) || (*kind != InstallKind::external && owner))
        return {{}, error(ErrorCode::invalid_receipt, "installation receipt ownership or host fields are invalid")};
      return {InstallReceipt{*format, *generation, *version, *host.value, *channel, *kind, owner ? std::optional<std::string>{*owner} : std::nullopt, *installRoot}, {}};
    }
    catch (const std::exception &e)
    {
      return {{}, error(ErrorCode::invalid_receipt, e.what())};
    }
  }

  Result<std::string> serialize_install_receipt(const InstallReceipt &receipt)
  {
    const auto host = parse_host_platform(receipt.host.canonical());
    if (receipt.format != 1 || receipt.generation.empty() || !parse_version(version_text(receipt.version)) || receipt.channel.empty() || receipt.installationRoot.empty() ||
        !host.valid() || (receipt.installKind == InstallKind::external && (!receipt.externalOwner || receipt.externalOwner->empty())) ||
        (receipt.installKind != InstallKind::external && receipt.externalOwner))
      return {{}, error(ErrorCode::invalid_receipt, "installation receipt is invalid")};
    std::ostringstream out;
    out << "format = 1\ngeneration = " << quote(receipt.generation) << "\nversion = " << quote(version_text(receipt.version))
        << "\nhost = " << quote(receipt.host.canonical()) << "\nchannel = " << quote(receipt.channel)
        << "\ninstall-kind = " << quote(install_kind_name(receipt.installKind)) << "\ninstallation-root = " << quote(receipt.installationRoot) << "\n";
    if (receipt.externalOwner)
      out << "external-owner = " << quote(*receipt.externalOwner) << "\n";
    return {out.str(), {}};
  }

  Result<InstallationLayout> installation_layout(const std::filesystem::path &root, const std::string &version)
  {
    if (root.empty() || !parse_version(version))
      return {{}, error(ErrorCode::invalid_value, "installation root or version is invalid")};
    InstallationLayout layout;
    layout.root = root;
    layout.versions = root / "versions";
    layout.versionRoot = layout.versions / version;
    layout.bin = layout.versionRoot / "bin";
    layout.include = layout.versionRoot / "include";
    layout.lib = layout.versionRoot / "lib";
    layout.sdk = layout.versionRoot / "sdk";
    layout.share = layout.versionRoot / "share";
    layout.current = root / "current";
    layout.receipt = root / "install.toml";
    return {std::move(layout), {}};
  }

  Result<std::filesystem::path> release_trust_store_path(const std::filesystem::path &installationRoot)
  {
    if (installationRoot.empty())
      return {{}, error(ErrorCode::invalid_value, "installation root is empty")};
    return {installationRoot / "share" / "vix" / "trust" / "releases.toml", {}};
  }

  Result<std::filesystem::path> distribution_home(const std::filesystem::path &parent, const std::string &generation)
  {
    if (parent.empty() || (generation != "current" && generation != "vix2"))
      return {{}, error(ErrorCode::invalid_value, "distribution generation/root is invalid")};
    return {parent / (generation == "current" ? "vix" : "vix2"), {}};
  }

  Result<UpgradePlan> plan_upgrade(const InstallReceipt &receipt, const VerifiedReleaseManifest &verifiedManifest, const HostPlatform &detectedHost)
  {
    const auto &manifest = verifiedManifest.manifest();
    if (receipt.generation != "current" || manifest.generation != "current")
      return {UpgradePlan{PlanKind::wrong_generation, receipt.version, manifest.version, {}, {}}, {}};
    if (receipt.installKind == InstallKind::external)
      return {UpgradePlan{PlanKind::externally_managed, receipt.version, manifest.version, {}, receipt.externalOwner}, {}};
    if (receipt.installKind != InstallKind::managed)
      return {UpgradePlan{PlanKind::archive_installation, receipt.version, manifest.version, {}, {}}, {}};
    if (receipt.channel != manifest.channel)
      return {UpgradePlan{PlanKind::channel_mismatch, receipt.version, manifest.version, {}, {}}, {}};
    if (!(receipt.host == detectedHost))
      return {UpgradePlan{PlanKind::unsupported_host, receipt.version, manifest.version, {}, {}}, {}};
    if (manifest.version == receipt.version)
      return {UpgradePlan{PlanKind::already_current, receipt.version, manifest.version, {}, {}}, {}};
    if (vix::cli::util::semver::compare(version_text(manifest.version), version_text(receipt.version)) <= 0)
      return {UpgradePlan{PlanKind::no_newer_release, receipt.version, manifest.version, {}, {}}, {}};
    const auto artifact = std::find_if(manifest.artifacts.begin(), manifest.artifacts.end(), [&](const auto &item)
                                       { return item.host == detectedHost; });
    if (artifact == manifest.artifacts.end())
      return {UpgradePlan{PlanKind::unsupported_host, receipt.version, manifest.version, {}, {}}, {}};
    return {UpgradePlan{PlanKind::upgrade_available, receipt.version, manifest.version, *artifact, {}}, {}};
  }
} // namespace vix::cli::distribution
