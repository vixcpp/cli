#include <vix/cli/project/native/RegistryMetadataSource.hpp>
#include <vix/cli/project/native/RegistryContentTransport.hpp>
#include <vix/cli/project/native/TomlParser.hpp>

#include <vix/crypto/signature.hpp>

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>
#include <vector>

using namespace vix::cli::project::native;

namespace
{
  std::string hex(std::span<const std::uint8_t> bytes)
  {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : bytes)
    { result.push_back(digits[byte >> 4]); result.push_back(digits[byte & 15]); }
    return result;
  }

  class MemoryMetadataTransport final : public RegistryMetadataTransport
  {
  public:
    std::map<std::string, std::string> files;
    std::vector<std::string> requested;
    std::vector<std::string> locations;
    std::string missingAt;
    std::string missingPath;
    RegistryMetadataTransportResult fetch(const RegistryLocation &location, std::string_view path,
      std::uint64_t maximumBytes) override
    {
      requested.emplace_back(path);
      locations.push_back(location.descriptor);
      if (location.descriptor == missingAt && path == missingPath)
        return {RegistryObservationLocationStatus::not_found, {}, "listed observation missing from mirror"};
      const auto found = files.find(std::string{path});
      if (found == files.end()) return {RegistryObservationLocationStatus::not_found, {}, "missing fixture"};
      if (found->second.size() > maximumBytes) return {RegistryObservationLocationStatus::integrity_failure, {}, "oversize fixture"};
      return {RegistryObservationLocationStatus::available, found->second, {}};
    }
  };

  class FakeHttpsExecutor final : public RegistryHttpsRequestExecutor
  {
  public:
    std::string url;
    std::string body{"metadata\0bytes", 14};
    std::map<std::string, std::string> scriptedBodies;
    int status{200};
    RegistryHttpsExecutionResult get(std::string_view requested, const HeadSink &headSink,
      const BodySink &bodySink) override
    {
      url = requested;
      const auto found = scriptedBodies.find(url);
      const std::string &responseBody = found == scriptedBodies.end() ? body : found->second;
      headSink(RegistryHttpsResponseHead{status, responseBody.size(), {}, std::string{"identity"}});
      if (!responseBody.empty()) bodySink(std::span<const std::byte>{reinterpret_cast<const std::byte *>(responseBody.data()), responseBody.size()});
      return {true, {}};
    }
  };

  struct Fixture
  {
    std::array<std::uint8_t, 32> publicKey{};
    std::array<std::uint8_t, 64> privateKey{};
    std::array<std::uint8_t, 64> signature{};
    RegistrySnapshotIdentity snapshot{"vix", 1, {}};
    RegistryPackageIdentity identity{"vix", "demo", "widget"};
    std::string manifestPath;
    std::string manifest;
    std::string packagePath;
    std::string packageDocument;
    std::string headPath{"metadata/v1/registries/vix/head.toml"};
    std::string head;

    explicit Fixture(bool withPackage = false)
    {
      assert(vix::crypto::signature_keygen(vix::crypto::SignatureAlg::ed25519, publicKey, privateKey).ok());
      std::vector<std::tuple<std::string, std::string, std::string>> packages;
      std::vector<RegistryCandidate> releases;
      std::string contractText;
      if (withPackage)
      {
        contractText = R"toml(format = 1
[package]
source = "registry"
registry = "vix"
publisher = "demo"
name = "widget"
version = "1.2.3"
[integration]
type = "native-vix"
include-roots = ["include"]
[[exports]]
name = "core"
project-target = "core"
)toml";
        auto parsed = parse_package_contract_text(contractText);
        assert(parsed.valid() && parsed.value);
        const Version version{1, 2, 3, std::nullopt};
        releases.push_back({identity, version, "sha256:" + std::string(64, 'b'), std::move(*parsed.value)});
        packages.emplace_back(identity.publisher, identity.name,
          registry_observation_content_digest(identity, 1, releases));
      }
      snapshot.digest = registry_snapshot_manifest_digest("vix", 1, packages);
      manifestPath = "metadata/v1/registries/vix/snapshots/1/sha256/" + snapshot.digest.substr(7) + "/snapshot.toml";
      if (withPackage)
      {
        manifest = "format = 1\nregistry-id = \"vix\"\nsequence = 1\npackages = [{ publisher = \"demo\", package-name = \"widget\", observation-digest = \"" +
          std::get<2>(packages.front()) + "\" }]\n";
        packagePath = "metadata/v1/registries/vix/snapshots/1/sha256/" + snapshot.digest.substr(7) +
          "/publishers/demo/packages/widget.toml";
        packageDocument = "format = 1\nregistry-id = \"vix\"\nsequence = 1\nsnapshot-digest = \"" + snapshot.digest +
          "\"\npublisher = \"demo\"\npackage-name = \"widget\"\n[[releases]]\nversion = \"1.2.3\"\ncontent-id = \"sha256:" +
          std::string(64, 'b') + "\"\ncontract = \"\"\"" + contractText + "\"\"\"\n";
      }
      else
        manifest = "format = 1\nregistry-id = \"vix\"\nsequence = 1\npackages = []\n";
      const auto payload = registry_head_signature_payload(snapshot, "vix-root", "key-1");
      assert(vix::crypto::sign(vix::crypto::SignatureAlg::ed25519, privateKey,
        std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t *>(payload.data()), payload.size()}, signature).ok());
      head = "format = 1\nregistry-id = \"vix\"\nsequence = 1\nsnapshot-digest = \"" + snapshot.digest +
        "\"\nauthority-id = \"vix-root\"\nkey-id = \"key-1\"\nsignature = \"" + hex(signature) + "\"\n";
    }

    MemoryMetadataTransport transport() const
    {
      MemoryMetadataTransport value;
      value.files.emplace(headPath, head);
      value.files.emplace(manifestPath, manifest);
      if (!packagePath.empty())
        value.files.emplace(packagePath, packageDocument);
      return value;
    }
  };

  void canonical_manifest_and_authority_tests()
  {
    const std::vector<std::tuple<std::string, std::string, std::string>> empty;
    const auto digest = registry_snapshot_manifest_digest("vix", 1, empty);
    assert(digest == registry_snapshot_manifest_digest("vix", 1, empty));
    const std::vector<std::tuple<std::string, std::string, std::string>> changed{{"demo", "widget", "sha256:" + std::string(64, 'a')}};
    assert(digest != registry_snapshot_manifest_digest("vix", 1, changed));

    const RegistryPackageIdentity package{"vix", "demo", "widget"};
    const Version version{1, 2, 3, std::nullopt};
    PackageContract contract;
    contract.identity = RegistryPackageContractIdentity{package, version};
    contract.integration = NativeVixIntegration{{"include"}};
    const RegistryCandidate release{package, version, "sha256:" + std::string(64, 'b'), contract};
    const std::vector<RegistryCandidate> releases{release};
    const auto observationDigest = registry_observation_content_digest(package, 1, releases);
    assert(observationDigest == registry_observation_content_digest(package, 1, releases));
    auto changedRelease = release;
    changedRelease.contentDigest = "sha256:" + std::string(64, 'c');
    assert(observationDigest != registry_observation_content_digest(package, 1, {changedRelease}));

    Fixture fixture;
    RegistryAuthorityDefinition authority{"vix", "vix-root"};
    auto authorities = std::vector<RegistryMetadataTrustedAuthority>{{"vix-root", {{"key-1", hex(fixture.publicKey)}}}};
    Ed25519RegistryAuthorityVerifier verifier{authorities};
    UnverifiedRegistryObservation observation{fixture.snapshot, fixture.identity, {}, "vix-root\nkey-1\n" + hex(fixture.signature)};
    std::string diagnostic;
    assert(verifier.verify_evidence(authority, observation, diagnostic));
    observation.authorityEvidence.back() = observation.authorityEvidence.back() == '0' ? '1' : '0';
    assert(!verifier.verify_evidence(authority, observation, diagnostic));
    observation.authorityEvidence = "vix-root\nunknown-key\n" + hex(fixture.signature);
    assert(!verifier.verify_evidence(authority, observation, diagnostic));
    Ed25519RegistryAuthorityVerifier unknown{std::vector<RegistryMetadataTrustedAuthority>{}};
    assert(!unknown.verify_evidence(authority, observation, diagnostic));
  }

  void authenticated_absence_pipeline_test()
  {
    Fixture fixture;
    auto transport = fixture.transport();
    TransportRegistryObservationLocationSource source{transport};
    RegistryLocation location{"https://fixture.invalid"};
    const auto response = source.registry_observation_at(location, fixture.identity);
    assert(response.status == RegistryObservationLocationStatus::available);
    assert(response.observation && response.observation->releases.empty());
    assert(transport.requested.size() == 2);
    assert(transport.requested[0] == fixture.headPath);
    assert(transport.requested[1] == fixture.manifestPath);

    transport.files[fixture.manifestPath] = "format = 1\nregistry-id = \"vix\"\nsequence = 2\npackages = []\n";
    const auto mismatched = source.registry_observation_at(location, fixture.identity);
    assert(mismatched.status == RegistryObservationLocationStatus::integrity_failure);
    transport.files[fixture.manifestPath] = fixture.manifest;

    auto directory = build_registry_directory({RegistryDefinition{"vix", {location}, {}}});
    auto authorityDirectory = build_registry_authority_directory({RegistryAuthorityDefinition{"vix", "vix-root"}});
    assert(directory.valid() && authorityDirectory.valid());
    Ed25519RegistryAuthorityVerifier verifier{{RegistryMetadataTrustedAuthority{"vix-root",
      {{"key-1", hex(fixture.publicKey)}}}}};
    auto routed = route_verified_registry_observation(*directory.value, *authorityDirectory.value,
      source, verifier, fixture.identity);
    assert(routed.valid() && routed.observation->releases().empty());
    InMemoryTrustedRegistryStateStore trusted;
    InMemoryPackageMetadataCache cache;
    RegistryOnlinePackageMetadataSource online{*directory.value, *authorityDirectory.value,
      source, verifier, trusted, cache};
    const auto releases = online.registry_releases(fixture.identity);
    assert(releases.succeeded && releases.releases.empty());
    const auto acceptedState = trusted.load("vix");
    assert(acceptedState.valid() && acceptedState.state && acceptedState.state->digest == fixture.snapshot.digest);
    const auto cached = cache.read_registry(fixture.snapshot, fixture.identity);
    assert(cached.valid() && cached.value && cached.value->releases.empty());

    Fixture packageFixture{true};
    auto packageTransport = packageFixture.transport();
    TransportRegistryObservationLocationSource packageSource{packageTransport};
    const auto packageObservation = packageSource.registry_observation_at(location, packageFixture.identity);
    assert(packageObservation.status == RegistryObservationLocationStatus::available);
    assert(packageObservation.observation && packageObservation.observation->releases.size() == 1);
    packageTransport.files[packageFixture.packagePath].replace(
      packageTransport.files[packageFixture.packagePath].find(std::string(64, 'b')), 64, std::string(64, 'c'));
    const auto wrongObservation = packageSource.registry_observation_at(location, packageFixture.identity);
    assert(wrongObservation.status == RegistryObservationLocationStatus::integrity_failure);
    packageTransport.files.erase(packageFixture.packagePath);
    const auto missingListedPackage = packageSource.registry_observation_at(location, packageFixture.identity);
    assert(missingListedPackage.status == RegistryObservationLocationStatus::not_found);

    auto fallbackTransport = packageFixture.transport();
    fallbackTransport.missingAt = "mirror-a";
    fallbackTransport.missingPath = packageFixture.packagePath;
    TransportRegistryObservationLocationSource fallbackSource{fallbackTransport};
    auto fallbackDirectory = build_registry_directory({RegistryDefinition{"vix", {{"mirror-a"}, {"mirror-b"}}, {}}});
    auto fallbackAuthorities = build_registry_authority_directory({RegistryAuthorityDefinition{"vix", "vix-root"}});
    assert(fallbackDirectory.valid() && fallbackAuthorities.valid());
    Ed25519RegistryAuthorityVerifier packageVerifier{{RegistryMetadataTrustedAuthority{"vix-root",
      {{"key-1", hex(packageFixture.publicKey)}}}}};
    const auto fallbackResult = route_verified_registry_observation(*fallbackDirectory.value,
      *fallbackAuthorities.value, fallbackSource, packageVerifier, packageFixture.identity);
    assert(fallbackResult.valid() && fallbackResult.observation->releases().size() == 1);
    assert(fallbackTransport.locations.size() == 6);
    assert(fallbackTransport.locations[0] == "mirror-a" && fallbackTransport.locations[3] == "mirror-b");
  }

  void concrete_transport_tests()
  {
    FakeHttpsExecutor executor;
    RequestsRegistryMetadataTransport https{executor};
    const RegistryLocation location{"https://registry.example/base/"};
    const auto response = https.fetch(location, "metadata/v1/registries/vix/head.toml", 128);
    assert(response.status == RegistryObservationLocationStatus::available);
    assert(response.bytes && *response.bytes == executor.body);
    assert(executor.url == "https://registry.example/base/metadata/v1/registries/vix/head.toml");

    const auto root = std::filesystem::temp_directory_path() /
      ("vix-registry-metadata-source-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    const auto path = root / "metadata/v1/registries/vix/head.toml";
    std::filesystem::create_directories(path.parent_path());
    { std::ofstream file(path, std::ios::binary); file.write(executor.body.data(), static_cast<std::streamsize>(executor.body.size())); }
    RequestsRegistryMetadataTransport fileTransport{executor};
    const auto fileResponse = fileTransport.fetch(RegistryLocation{"file://" + root.string()},
      "metadata/v1/registries/vix/head.toml", 128);
    assert(fileResponse.status == RegistryObservationLocationStatus::available);
    assert(fileResponse.bytes && *fileResponse.bytes == executor.body);

    Fixture fixture;
    const auto fileRoot = root / "complete-mirror";
    const auto headPath = fileRoot / fixture.headPath;
    const auto manifestPath = fileRoot / fixture.manifestPath;
    std::filesystem::create_directories(headPath.parent_path());
    std::filesystem::create_directories(manifestPath.parent_path());
    { std::ofstream file(headPath, std::ios::binary); file << fixture.head; }
    { std::ofstream file(manifestPath, std::ios::binary); file << fixture.manifest; }
    const auto fileLocation = RegistryLocation{"file://" + fileRoot.string()};
    TransportRegistryObservationLocationSource fileSource{fileTransport};
    const auto fileObservation = fileSource.registry_observation_at(fileLocation, fixture.identity);
    assert(fileObservation.status == RegistryObservationLocationStatus::available);
    assert(fileObservation.observation && fileObservation.observation->releases.empty());

    const std::string httpsBase{"https://registry.example"};
    const std::string httpsHeadUrl = httpsBase + "/" + fixture.headPath;
    const std::string httpsManifestUrl = httpsBase + "/" + fixture.manifestPath;
    executor.scriptedBodies.emplace(httpsHeadUrl, fixture.head);
    executor.scriptedBodies.emplace(httpsManifestUrl, fixture.manifest);
    TransportRegistryObservationLocationSource httpsSource{https};
    const auto httpsObservation = httpsSource.registry_observation_at(RegistryLocation{httpsBase}, fixture.identity);
    assert(httpsObservation.status == RegistryObservationLocationStatus::available);
    assert(httpsObservation.observation && httpsObservation.observation->releases.empty());
    std::filesystem::remove_all(root, ignored);
  }
}

int main()
{
  canonical_manifest_and_authority_tests();
  authenticated_absence_pipeline_test();
  concrete_transport_tests();
}
