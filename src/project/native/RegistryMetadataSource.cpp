#include <vix/cli/project/native/RegistryMetadataSource.hpp>

#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/PackageContentStore.hpp>
#include <vix/cli/project/native/RegistryContentTransport.hpp>
#include <vix/cli/project/native/TomlParser.hpp>
#include <vix/cli/project/native/Validation.hpp>

#include <toml.hpp>
#include <vix/crypto/signature.hpp>
#include <vix/requests/Url.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <span>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

namespace vix::cli::project::native
{
  namespace
  {
    using Table = toml::table;
    void field(std::string &out, std::string_view name, std::string_view value)
    {
      out.append(std::to_string(name.size())).push_back(':'); out.append(name);
      out.append(std::to_string(value.size())).push_back(':'); out.append(value);
    }
    void number(std::string &out, std::string_view name, std::uint64_t value)
    { field(out, name, std::to_string(value)); }

    std::string version_semantics(const Version &v)
    {
      std::string out; number(out, "major", v.major); number(out, "minor", v.minor); number(out, "patch", v.patch);
      field(out, "prerelease", v.prerelease.value_or("")); return out;
    }
    std::string requirement_semantics(const PackageRequirement &r)
    {
      std::string out; field(out, "alias", r.alias);
      if (const auto *reg = std::get_if<RegistryRequirement>(&r.source))
      {
        field(out, "source", "registry"); field(out, "registry", reg->identity.registry);
        field(out, "publisher", reg->identity.publisher); field(out, "package", reg->identity.name);
        field(out, "version-kind", std::holds_alternative<ExactVersion>(reg->version) ? "exact" : "compatible");
        field(out, "version", version_semantics(std::visit([](const auto &v) -> const Version & { return v.version; }, reg->version)));
      }
      else
      {
        const auto &git = std::get<GitRequirement>(r.source); field(out, "source", "git");
        field(out, "repository", git.normalizedRepository); field(out, "repository-root", git.repositoryRoot ? "true" : "false");
        field(out, "revision-kind", std::visit([](const auto &v) -> std::string_view
          { using T = std::decay_t<decltype(v)>; if constexpr (std::is_same_v<T, CommitRevision>) return "commit";
            else if constexpr (std::is_same_v<T, TagRevision>) return "tag"; else return "branch"; }, git.revision));
        field(out, "revision", std::visit([](const auto &v) -> const std::string & { return v.value; }, git.revision));
      }
      return out;
    }
    std::string contract_semantics(const PackageContract &c)
    {
      std::string out; number(out, "format", static_cast<std::uint64_t>(c.format));
      if (const auto *reg = std::get_if<RegistryPackageContractIdentity>(&c.identity))
      {
        field(out, "identity-source", "registry"); field(out, "registry", reg->identity.registry);
        field(out, "publisher", reg->identity.publisher); field(out, "package", reg->identity.name);
        field(out, "version", version_semantics(reg->version));
      }
      else field(out, "identity-source", "git");
      number(out, "dependency-count", c.packages.size());
      for (const auto &p : c.packages) field(out, "dependency", requirement_semantics(p));
      if (const auto *native = std::get_if<NativeVixIntegration>(&c.integration))
      {
        field(out, "integration", "native-vix"); number(out, "include-root-count", native->includeRoots.size());
        for (const auto &v : native->includeRoots) field(out, "include-root", v);
      }
      else
      {
        const std::string integration = "cmake-config:" + std::get<CMakeConfigIntegration>(c.integration).configName;
        field(out, "integration", integration);
      }
      number(out, "export-count", c.exports.size());
      for (const auto &e : c.exports)
      {
        field(out, "export-name", e.name);
        if (const auto *n = std::get_if<NativeExportMapping>(&e.mapping)) field(out, "export-target", n->projectTarget);
        else field(out, "export-target", std::get<CMakeExportMapping>(e.mapping).cmakeTarget);
      }
      return out;
    }
    std::optional<std::string> string_value(const Table &t, std::string_view key)
    {
      const auto *n = t.get(key); if (!n) return std::nullopt;
      const auto v = n->value<std::string>(); return v ? std::optional<std::string>{*v} : std::nullopt;
    }
    std::optional<std::uint64_t> integer_value(const Table &t, std::string_view key)
    {
      const auto *n = t.get(key); if (!n) return std::nullopt;
      const auto v = n->value<std::int64_t>();
      if (!v || *v < 0) return std::nullopt;
      return static_cast<std::uint64_t>(*v);
    }
    bool digest_hex(std::string_view value)
    { return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c)
      { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }); }
    std::optional<std::vector<std::uint8_t>> decode_hex(std::string_view value)
    {
      if (value.size() % 2) return std::nullopt;
      std::vector<std::uint8_t> out; out.reserve(value.size() / 2);
      auto digit = [](char c) -> int
      {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      for (std::size_t i = 0; i < value.size(); i += 2)
      {
        const int a = digit(value[i]), b = digit(value[i + 1]);
        if (a < 0 || b < 0) return std::nullopt;
        out.push_back(static_cast<std::uint8_t>((a << 4) | b)); }
      return out;
    }
    std::string head_payload(const RegistrySnapshotIdentity &s, std::string_view authority,
      std::string_view key)
    {
      std::string out{"VIX-REGISTRY-HEAD-v1\n"}; number(out, "format", 1); field(out, "registry-id", s.registryId);
      number(out, "sequence", s.sequence); field(out, "snapshot-digest", s.digest);
      field(out, "authority-id", authority); field(out, "key-id", key); return out;
    }
    UnverifiedRegistryLocationResult location_failure(RegistryObservationLocationStatus status, std::string msg)
    { return {status, std::nullopt, std::move(msg)}; }
    bool safe_component(std::string_view v)
    { return !v.empty() && v != "." && v != ".." && v.find_first_of("/\\") == std::string_view::npos &&
        v.find('\0') == std::string_view::npos; }
    bool only_keys(const Table &table, std::initializer_list<std::string_view> allowed)
    {
      for (const auto &[key, value] : table)
      {
        static_cast<void>(value);
        const auto name = key.str();
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) return false;
      }
      return true;
    }
    std::string snapshot_base(std::string_view registry, std::uint64_t sequence, std::string_view digest)
    { return "metadata/v1/registries/" + std::string{registry} + "/snapshots/" + std::to_string(sequence) +
        "/sha256/" + std::string{digest}; }
    std::optional<Version> parse_version(std::string_view value)
    {
      Version v; const auto dash = value.find('-'); const auto core = value.substr(0, dash);
      std::uint64_t *parts[] = {&v.major, &v.minor, &v.patch}; std::size_t start = 0;
      for (unsigned i = 0; i < 3; ++i)
      { const auto end = core.find('.', start); const auto token = core.substr(start, end == std::string_view::npos ? core.size() - start : end - start);
        if (token.empty()) return std::nullopt;
        auto parsed = std::from_chars(token.data(), token.data() + token.size(), *parts[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) return std::nullopt;
        if (i < 2 && end == std::string_view::npos) return std::nullopt;
        start = end + 1; }
      if (dash != std::string_view::npos) v.prerelease = std::string{value.substr(dash + 1)};
      std::string canonical = std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
      if (v.prerelease) canonical += "-" + *v.prerelease;
      if (canonical != value) return std::nullopt;
      return v;
    }
    bool version_less(const Version &a, const Version &b)
    {
      if (a.major != b.major) return a.major < b.major;
      if (a.minor != b.minor) return a.minor < b.minor;
      if (a.patch != b.patch) return a.patch < b.patch;
      if (a.prerelease.has_value() != b.prerelease.has_value()) return a.prerelease.has_value();
      return a.prerelease.value_or("") < b.prerelease.value_or("");
    }
    std::vector<std::tuple<std::string, std::string, std::string>> manifest_entries(const toml::array &array, bool &valid)
    {
      std::vector<std::tuple<std::string, std::string, std::string>> out;
      valid = true;
      for (const auto &node : array)
      {
        const auto *t = node.as_table(); if (!t) { valid = false; return {}; }
        if (!only_keys(*t, {"publisher", "package-name", "observation-digest"}))
        { valid = false; return {}; }
        auto publisher = string_value(*t, "publisher"), name = string_value(*t, "package-name"), digest = string_value(*t, "observation-digest");
        if (!publisher || !name || !digest || !valid_registry_name(*publisher) || !valid_registry_name(*name) ||
            !digest->starts_with("sha256:") || !digest_hex(std::string_view{*digest}.substr(7)))
        { valid = false; return {}; }
        out.emplace_back(*publisher, *name, *digest);
      }
      auto less = [](const auto &a, const auto &b)
      { return std::tie(std::get<0>(a), std::get<1>(a)) < std::tie(std::get<0>(b), std::get<1>(b)); };
      if (!std::is_sorted(out.begin(), out.end(), less)) { valid = false; return {}; }
      for (std::size_t i = 1; i < out.size(); ++i)
        if (std::get<0>(out[i - 1]) == std::get<0>(out[i]) && std::get<1>(out[i - 1]) == std::get<1>(out[i]))
        { valid = false; return {}; }
      return out;
    }
  }

  std::string registry_observation_content_digest(const RegistryPackageIdentity &query, std::uint64_t sequence,
    const std::vector<RegistryCandidate> &releases)
  {
    std::string bytes{"VIX-REGISTRY-OBSERVATION-CONTENT-v1\n"}; number(bytes, "format", 1);
    field(bytes, "registry-id", query.registry); number(bytes, "sequence", sequence);
    field(bytes, "publisher", query.publisher); field(bytes, "package-name", query.name);
    number(bytes, "release-count", releases.size());
    for (const auto &r : releases)
    {
      field(bytes, "release-registry", r.identity.registry); field(bytes, "release-publisher", r.identity.publisher);
      field(bytes, "release-package", r.identity.name); field(bytes, "version", version_semantics(r.version));
      field(bytes, "content-id", r.contentDigest); field(bytes, "contract", contract_semantics(r.contract));
    }
    return sha256_digest(bytes);
  }

  std::string registry_snapshot_manifest_digest(std::string_view registryId, std::uint64_t sequence,
    const std::vector<std::tuple<std::string, std::string, std::string>> &packages)
  {
    std::string bytes{"VIX-REGISTRY-SNAPSHOT-v1\n"}; number(bytes, "format", 1);
    field(bytes, "registry-id", registryId); number(bytes, "sequence", sequence); number(bytes, "package-count", packages.size());
    for (const auto &[publisher, name, digest] : packages)
    { field(bytes, "publisher", publisher); field(bytes, "package-name", name); field(bytes, "observation-digest", digest); }
    return sha256_digest(bytes);
  }

  std::string registry_head_signature_payload(const RegistrySnapshotIdentity &snapshot,
    std::string_view authorityId, std::string_view keyId)
  { return head_payload(snapshot, authorityId, keyId); }

  Ed25519RegistryAuthorityVerifier::Ed25519RegistryAuthorityVerifier(
    std::vector<RegistryMetadataTrustedAuthority> authorities) : authorities_(std::move(authorities)) {}

  bool Ed25519RegistryAuthorityVerifier::verify_evidence(const RegistryAuthorityDefinition &authority,
    const UnverifiedRegistryObservation &observation, std::string &diagnostic) const
  {
    const auto first = observation.authorityEvidence.find('\n');
    const auto second = first == std::string::npos ? first : observation.authorityEvidence.find('\n', first + 1);
    if (first == std::string::npos || second == std::string::npos ||
        observation.authorityEvidence.find('\n', second + 1) != std::string::npos)
    { diagnostic = "malformed Registry authority evidence"; return false; }
    const auto authorityId = std::string_view{observation.authorityEvidence}.substr(0, first);
    const auto keyId = std::string_view{observation.authorityEvidence}.substr(first + 1, second - first - 1);
    const auto signatureHex = std::string_view{observation.authorityEvidence}.substr(second + 1);
    if (authorityId.empty() || keyId.empty() || signatureHex.empty())
    { diagnostic = "Registry authority evidence contains an empty identity or signature"; return false; }
    const auto trusted = std::find_if(authorities_.begin(), authorities_.end(), [&](const auto &v)
      { return v.authorityId == authorityId; });
    if (authorityId != authority.authorityId || trusted == authorities_.end() ||
        std::count_if(authorities_.begin(), authorities_.end(), [&](const auto &v) { return v.authorityId == authorityId; }) != 1)
    { diagnostic = "unknown Registry authority"; return false; }
    const auto key = std::find_if(trusted->keys.begin(), trusted->keys.end(), [&](const auto &v) { return v.keyId == keyId; });
    if (key == trusted->keys.end() ||
        std::count_if(trusted->keys.begin(), trusted->keys.end(), [&](const auto &v) { return v.keyId == keyId; }) != 1)
    { diagnostic = "unknown or ambiguous Registry authority key"; return false; }
    const auto publicKey = decode_hex(key->publicKeyHex), signature = decode_hex(signatureHex);
    if (!publicKey || publicKey->size() != 32 || !signature || signature->size() != 64)
    { diagnostic = "Registry Ed25519 key or signature has invalid size/encoding"; return false; }
    const auto payload = head_payload(observation.snapshot, authorityId, keyId);
    const auto verified = vix::crypto::verify(vix::crypto::SignatureAlg::ed25519, *publicKey,
      std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t *>(payload.data()), payload.size()}, *signature);
    if (!verified.ok()) { diagnostic = "Registry head Ed25519 signature is invalid"; return false; }
    return true;
  }

  RegistryMetadataTransportResult RequestsRegistryMetadataTransport::fetch(const RegistryLocation &location,
    std::string_view relativePath, std::uint64_t maximumBytes)
  {
    if (relativePath.empty() || relativePath.front() == '/' || relativePath.find("..") != std::string_view::npos)
      return {RegistryObservationLocationStatus::configuration_error, std::nullopt, "invalid Registry metadata relative path"};
    if (location.descriptor.starts_with("file://"))
    {
      std::string base = location.descriptor.substr(7);
      if (base.empty() || base.front() != '/' || base.starts_with("//") ||
          base.find_first_of("?#") != std::string::npos || base.find('\0') != std::string::npos)
        return {RegistryObservationLocationStatus::configuration_error, std::nullopt, "malformed local Registry metadata location"};
      std::filesystem::path path{base};
      std::filesystem::path prefix;
      for (const auto &component : path)
      {
        prefix /= component;
        std::error_code prefixError;
        const auto prefixStatus = std::filesystem::symlink_status(prefix, prefixError);
        if (prefixError == std::errc::no_such_file_or_directory || prefixStatus.type() == std::filesystem::file_type::not_found)
          break;
        if (prefixError == std::errc::permission_denied)
          return {RegistryObservationLocationStatus::permission_denied, std::nullopt, "Registry metadata mirror access denied"};
        if (prefixError || std::filesystem::is_symlink(prefixStatus))
          return {RegistryObservationLocationStatus::configuration_error, std::nullopt, "Registry metadata mirror path contains a symlink or invalid component"};
      }
      std::size_t start = 0;
      while (start < relativePath.size())
      { auto end = relativePath.find('/', start); auto component = relativePath.substr(start, end == std::string_view::npos ? relativePath.size() - start : end - start);
        if (!safe_component(component)) return {RegistryObservationLocationStatus::configuration_error, std::nullopt, "unsafe Registry metadata path"};
        path /= std::string{component};
        std::error_code componentError;
        const auto componentStatus = std::filesystem::symlink_status(path, componentError);
        if (componentError == std::errc::no_such_file_or_directory || componentStatus.type() == std::filesystem::file_type::not_found)
        { if (end == std::string_view::npos) return {RegistryObservationLocationStatus::not_found, std::nullopt, "Registry metadata object not found"}; }
        else if (componentError == std::errc::permission_denied)
          return {RegistryObservationLocationStatus::permission_denied, std::nullopt, "Registry metadata mirror access denied"};
        else if (componentError || std::filesystem::is_symlink(componentStatus))
          return {RegistryObservationLocationStatus::configuration_error, std::nullopt, "Registry metadata path contains a symlink or invalid component"};
        if (end == std::string_view::npos) break;
        start = end + 1; }
      std::error_code ec; const auto status = std::filesystem::symlink_status(path, ec);
      if (ec == std::errc::no_such_file_or_directory || status.type() == std::filesystem::file_type::not_found)
        return {RegistryObservationLocationStatus::not_found, std::nullopt, "Registry metadata object not found"};
      if (ec == std::errc::permission_denied) return {RegistryObservationLocationStatus::permission_denied, std::nullopt, "Registry metadata read denied"};
      if (ec || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
        return {RegistryObservationLocationStatus::configuration_error, std::nullopt, "Registry metadata object is not a regular non-symlink file"};
      const auto size = std::filesystem::file_size(path, ec);
      if (ec) return {RegistryObservationLocationStatus::transport_failure, std::nullopt, "cannot inspect Registry metadata object"};
      if (size > maximumBytes || size > std::numeric_limits<std::size_t>::max() ||
          size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        return {RegistryObservationLocationStatus::integrity_failure, std::nullopt, "Registry metadata object exceeds size limit"};
      std::ifstream input(path, std::ios::binary); if (!input) return {RegistryObservationLocationStatus::permission_denied, std::nullopt, "cannot open Registry metadata object"};
      std::string bytes(static_cast<std::size_t>(size), '\0'); input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      if (!input || input.peek() != std::char_traits<char>::eof()) return {RegistryObservationLocationStatus::transport_failure, std::nullopt, "Registry metadata read was incomplete"};
      return {RegistryObservationLocationStatus::available, std::move(bytes), {}};
    }
    if (!location.descriptor.starts_with("https://") || location.descriptor.find_first_of("?#@") != std::string::npos)
      return {RegistryObservationLocationStatus::configuration_error, std::nullopt, "Registry metadata location must be HTTPS or local file"};
    std::string url;
    try
    {
      auto base = vix::requests::Url::parse(location.descriptor);
      if (!base.is_https() || !base.query().empty() || !base.fragment().empty())
        return {RegistryObservationLocationStatus::configuration_error, std::nullopt, "Registry metadata location is not a valid HTTPS base"};
      std::string path = base.path(); while (!path.empty() && path.back() == '/') path.pop_back();
      url = base.origin() + path + "/" + std::string{relativePath};
    }
    catch (...) { return {RegistryObservationLocationStatus::configuration_error, std::nullopt, "malformed HTTPS Registry metadata location"}; }
    std::string currentUrl = std::move(url);
    for (std::size_t redirects = 0; ; )
    {
      std::string bytes; std::optional<RegistryHttpsResponseHead> head; bool advertisedOversize = false;
      RegistryHttpsExecutionResult result;
      try
      {
        result = executor_.get(currentUrl, [&](const auto &v)
        {
          head = v;
          if (v.statusCode == 200 && v.contentLength && *v.contentLength > maximumBytes)
          { advertisedOversize = true; throw std::length_error("Registry metadata exceeds size limit"); }
        }, [&](std::span<const std::byte> chunk)
        {
          if (head && head->statusCode != 200) return;
          const auto currentSize = static_cast<std::uint64_t>(bytes.size());
          if (currentSize > maximumBytes || chunk.size() > maximumBytes - currentSize) throw std::length_error("limit");
          bytes.append(reinterpret_cast<const char *>(chunk.data()), chunk.size());
        });
      }
      catch (const std::length_error &)
      { return {RegistryObservationLocationStatus::integrity_failure, std::nullopt, "Registry metadata exceeds size limit"}; }
      catch (const std::exception &error)
      { return {RegistryObservationLocationStatus::transport_failure, std::nullopt, error.what()}; }
      if (advertisedOversize)
        return {RegistryObservationLocationStatus::integrity_failure, std::nullopt, "Registry metadata exceeds size limit"};
      if (!result.completed || !head) return {RegistryObservationLocationStatus::transport_failure, std::nullopt, result.diagnostic};
      if (head->statusCode == 404 || head->statusCode == 410) return {RegistryObservationLocationStatus::not_found, std::nullopt, "Registry metadata object not found"};
      if (head->statusCode == 401 || head->statusCode == 403) return {RegistryObservationLocationStatus::permission_denied, std::nullopt, "Registry metadata access denied"};
      if (head->statusCode == 301 || head->statusCode == 302 || head->statusCode == 303 || head->statusCode == 307 || head->statusCode == 308)
      {
        if (!head->location || redirects >= 5)
          return {RegistryObservationLocationStatus::transport_failure, std::nullopt, "Registry metadata redirect missing Location or exceeded limit"};
        try
        {
          const auto base = vix::requests::Url::parse(currentUrl);
          const auto nextText = vix::requests::resolve_url_reference(base, *head->location);
          const auto next = vix::requests::Url::parse(nextText);
          if (!next.is_https() || nextText.find('@') != std::string::npos)
            return {RegistryObservationLocationStatus::transport_failure, std::nullopt, "Registry metadata redirect violates HTTPS policy"};
          currentUrl = next.without_fragment();
        }
        catch (const std::exception &)
        { return {RegistryObservationLocationStatus::transport_failure, std::nullopt, "Registry metadata redirect is invalid"}; }
        ++redirects;
        continue;
      }
      if (head->statusCode != 200) return {RegistryObservationLocationStatus::transport_failure, std::nullopt, "Registry metadata HTTPS request failed"};
      if (head->contentEncoding && *head->contentEncoding != "identity")
        return {RegistryObservationLocationStatus::malformed_metadata, std::nullopt, "Registry metadata response used unsupported content encoding"};
      if (head->contentLength && *head->contentLength > maximumBytes)
        return {RegistryObservationLocationStatus::integrity_failure, std::nullopt, "Registry metadata exceeds size limit"};
      return {RegistryObservationLocationStatus::available, std::move(bytes), {}};
    }
  }

  UnverifiedRegistryLocationResult TransportRegistryObservationLocationSource::registry_observation_at(
    const RegistryLocation &location, const RegistryPackageIdentity &identity)
  {
    if (!valid_registry_id(identity.registry) || !valid_registry_name(identity.publisher) || !valid_registry_name(identity.name))
      return location_failure(RegistryObservationLocationStatus::configuration_error, "Registry package query identity is malformed");
    const auto headPath = "metadata/v1/registries/" + identity.registry + "/head.toml";
    const auto headBytes = transport_.fetch(location, headPath, maximumBytes_);
    if (headBytes.status != RegistryObservationLocationStatus::available)
      return location_failure(headBytes.status, headBytes.diagnostic);
    if (!headBytes.bytes)
      return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry metadata transport omitted head bytes");
    try
    {
      const auto head = toml::parse(*headBytes.bytes);
      if (!only_keys(head, {"format", "registry-id", "sequence", "snapshot-digest", "authority-id", "key-id", "signature"}))
        return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry head contains unknown fields");
      auto format = integer_value(head, "format"), sequence = integer_value(head, "sequence");
      auto registry = string_value(head, "registry-id"), digest = string_value(head, "snapshot-digest");
      auto authority = string_value(head, "authority-id"), key = string_value(head, "key-id"), signature = string_value(head, "signature");
      if (!format || *format != 1 || !sequence || !*sequence || !registry || !valid_registry_id(*registry) ||
          !digest || !authority || authority->empty() || !key || key->empty() || !signature ||
          !digest->starts_with("sha256:") || !digest_hex(std::string_view{*digest}.substr(7)))
        return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry head fields are malformed");
      if (*registry != identity.registry) return location_failure(RegistryObservationLocationStatus::integrity_failure, "Registry head identity mismatch");
      if (!safe_component(*registry))
        return location_failure(RegistryObservationLocationStatus::integrity_failure, "Registry head contains an unsafe Registry ID");
      const auto base = snapshot_base(*registry, *sequence, std::string_view{*digest}.substr(7));
      const auto manifestBytes = transport_.fetch(location, base + "/snapshot.toml", maximumBytes_);
      if (manifestBytes.status != RegistryObservationLocationStatus::available)
        return location_failure(manifestBytes.status, manifestBytes.diagnostic);
      if (!manifestBytes.bytes)
        return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry metadata transport omitted snapshot bytes");
      const auto manifest = toml::parse(*manifestBytes.bytes);
      if (!only_keys(manifest, {"format", "registry-id", "sequence", "packages"}))
        return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry snapshot manifest contains unknown fields");
      auto mf = integer_value(manifest, "format"), ms = integer_value(manifest, "sequence");
      auto mr = string_value(manifest, "registry-id");
      const auto *arrayNode = manifest.get("packages"); const auto *array = arrayNode ? arrayNode->as_array() : nullptr;
      bool entriesValid = false;
      std::vector<std::tuple<std::string, std::string, std::string>> entries;
      if (array) entries = manifest_entries(*array, entriesValid);
      if (!mf || *mf != 1 || !ms || !*ms || !mr || !valid_registry_id(*mr) || !array || !entriesValid)
        return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry snapshot manifest is malformed or unsorted");
      if (*mr != *registry || *ms != *sequence)
        return location_failure(RegistryObservationLocationStatus::integrity_failure, "Registry snapshot manifest identity mismatch");
      const auto computed = registry_snapshot_manifest_digest(*mr, *ms, entries);
      if (computed != *digest)
        return location_failure(RegistryObservationLocationStatus::integrity_failure, "Registry snapshot manifest digest mismatch");
      const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto &v)
        { return std::get<0>(v) == identity.publisher && std::get<1>(v) == identity.name; });
      std::vector<RegistryCandidate> releases;
      if (entry != entries.end())
      {
        const auto packagePath = base + "/publishers/" + identity.publisher + "/packages/" + identity.name + ".toml";
        const auto packageBytes = transport_.fetch(location, packagePath, maximumBytes_);
        if (packageBytes.status != RegistryObservationLocationStatus::available)
          return location_failure(packageBytes.status, packageBytes.diagnostic);
        if (!packageBytes.bytes)
          return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry metadata transport omitted package observation bytes");
        const auto package = toml::parse(*packageBytes.bytes);
        if (!only_keys(package, {"format", "registry-id", "sequence", "snapshot-digest", "publisher", "package-name", "releases"}))
          return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry package observation contains unknown fields");
        auto pf = integer_value(package, "format"), ps = integer_value(package, "sequence");
        auto pr = string_value(package, "registry-id"), pd = string_value(package, "snapshot-digest");
        auto pp = string_value(package, "publisher"), pn = string_value(package, "package-name");
        const auto *releaseNode = package.get("releases"); const auto *releaseArray = releaseNode ? releaseNode->as_array() : nullptr;
        if (!pf || *pf != 1 || !ps || !pr || !pd || !pp || !pn || !releaseArray)
          return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry package observation fields are malformed");
        if (*ps != *sequence || *pr != *registry || *pd != *digest || *pp != identity.publisher || *pn != identity.name)
          return location_failure(RegistryObservationLocationStatus::integrity_failure, "Registry package observation binding mismatch");
        for (const auto &node : *releaseArray)
        {
          const auto *rt = node.as_table(); if (!rt) return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry release entry is malformed");
          if (!only_keys(*rt, {"version", "content-id", "contract"}))
            return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry release contains unknown fields");
          auto versionText = string_value(*rt, "version"), content = string_value(*rt, "content-id"), contractText = string_value(*rt, "contract");
          if (!versionText || !content || !contractText || !content->starts_with("sha256:") || !digest_hex(std::string_view{*content}.substr(7)))
            return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry release fields are malformed");
          auto version = parse_version(*versionText); auto parsedContent = PackageContentId::parse(*content); auto contract = parse_package_contract_text(*contractText);
          if (!version || !valid_semver(*version) || !parsedContent || !contract.valid() || !contract.value)
            return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry release contract is malformed");
          auto *contractIdentity = std::get_if<RegistryPackageContractIdentity>(&contract.value->identity);
          if (!contractIdentity || contractIdentity->identity != identity || contractIdentity->version != *version)
            return location_failure(RegistryObservationLocationStatus::integrity_failure, "Registry release contract identity mismatch");
          releases.push_back({identity, *version, *content, std::move(*contract.value)});
        }
        if (!std::is_sorted(releases.begin(), releases.end(), [](const auto &a, const auto &b) { return version_less(a.version, b.version); }))
          return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry releases are not deterministically ordered");
        for (std::size_t i = 1; i < releases.size(); ++i)
          if (releases[i - 1].version == releases[i].version)
            return location_failure(RegistryObservationLocationStatus::malformed_metadata, "Registry package observation contains a duplicate version");
        if (registry_observation_content_digest(identity, *sequence, releases) != std::get<2>(*entry))
          return location_failure(RegistryObservationLocationStatus::integrity_failure, "Registry package observation digest mismatch");
      }
      UnverifiedRegistryObservation observation{{*registry, *sequence, *digest}, identity, std::move(releases),
        *authority + "\n" + *key + "\n" + *signature};
      return {RegistryObservationLocationStatus::available, std::move(observation), {}};
    }
    catch (const std::exception &error)
    { return location_failure(RegistryObservationLocationStatus::malformed_metadata, std::string{"Registry metadata parse failed: "} + error.what()); }
  }

  RegistryQueryResult RegistryOnlinePackageMetadataSource::registry_releases(const RegistryPackageIdentity &identity)
  {
    if (!locations_.find(identity.registry))
      return {false, {}, "unknown_registry [" + identity.registry + "]: no Registry routing definition"};
    if (!authorities_.find(identity.registry))
      return {false, {}, "unknown_registry_authority [" + identity.registry + "]: no Registry authority definition"};
    const auto trusted = trustedStates_.load(identity.registry);
    if (!trusted.valid()) return {false, {}, "trusted Registry state could not be loaded; online admission stopped"};
    auto routed = route_verified_registry_observation(locations_, authorities_, source_, verifier_, identity);
    if (!routed.valid() || !routed.observation || !routed.unverifiedObservation)
      return {false, {}, routed.issues.empty() ? "Registry routing did not produce an authenticated observation" : routed.issues.front().message};
    const auto pin = std::lower_bound(pins_.begin(), pins_.end(), identity.registry,
      [](const auto &value, std::string_view registry) { return value.registryId < registry; });
    const bool hasPin = pin != pins_.end() && pin->registryId == identity.registry;

    const auto &raw = *routed.unverifiedObservation;
    if (hasPin && pin->snapshot != raw.snapshot)
      return {false, {}, "Registry snapshot changed during this metadata acquisition session"};
    const auto admission = admit_online_registry_observation(authorities_, verifier_, trusted.state,
      identity, raw, cache_);
    if (!admission.valid() || !admission.proposedState)
      return {false, {}, admission.issues.empty() ? "Registry observation failed authority or anti-rollback admission" : admission.issues.front().message};
    const auto committed = trustedStates_.compare_and_advance(trusted.state, *admission.proposedState);
    if (!committed.valid())
      return {false, {}, committed.issues.empty() ? "trusted Registry state could not be advanced" : committed.issues.front().message};
    if (!hasPin)
      pins_.insert(pin, RegistrySnapshotPin{identity.registry, raw.snapshot});
    return {true, routed.observation->releases(), {}};
  }

  GitQueryResult RegistryOnlinePackageMetadataSource::resolve_git(const GitPackageIdentity &,
    const GitRevisionRequirement &)
  { return {GitQueryStatus::failed, std::nullopt, "Git online metadata is not implemented in P10S-0C1"}; }
}
