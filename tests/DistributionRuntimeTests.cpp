#include <vix/cli/distribution/Runtime.hpp>
#include <vix/crypto/hex.hpp>
#include <vix/crypto/signature.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include <zlib.h>

using namespace vix::cli::distribution;
namespace fs = std::filesystem;

namespace
{
  std::vector<std::uint8_t> hex_bytes(const std::string &text)
  {
    std::vector<std::uint8_t> output(text.size() / 2);
    for (std::size_t i = 0; i < output.size(); ++i)
    {
      const auto digit = [](char c) -> unsigned
      { return c <= '9' ? static_cast<unsigned>(c - '0') : static_cast<unsigned>(c - 'a' + 10); };
      output[i] = static_cast<std::uint8_t>((digit(text[i * 2]) << 4) | digit(text[i * 2 + 1]));
    }
    return output;
  }

  struct FakeSource final : ReleaseManifestSource
  {
    SignedReleaseManifest value;
    int calls{0};
    Result<SignedReleaseManifest> fetch(const std::string &) override
    {
      ++calls;
      return {value, {}};
    }
  };

  struct FakeFetcher final : DistributionArtifactFetcher
  {
    std::string bytes{"archive-bytes"};
    int calls{0};
    Result<fs::path> fetch(const std::string &, const fs::path &path) override
    {
      ++calls;
      std::error_code ec;
      fs::create_directories(path.parent_path(), ec);
      std::ofstream output(path, std::ios::binary | std::ios::trunc);
      output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      if (!output) return {{}, Error{ErrorCode::artifact_fetch_failed, "fake write failed"}};
      return {path, {}};
    }
  };

  struct FakeExtractor final : ArchiveExtractor
  {
    int calls{0};
    bool fail{false};
    bool omitRequiredStructure{false};
    Result<fs::path> extract(ArchiveFormat, const fs::path &, const fs::path &staging) override
    {
      ++calls;
      if (fail) return {{}, Error{ErrorCode::unsafe_archive, "injected unsafe archive"}};
      const auto root = staging / "distribution";
      std::error_code ec;
      if (!omitRequiredStructure)
      {
        fs::create_directories(root / "bin", ec);
        fs::create_directories(root / "share" / "vix", ec);
        std::ofstream(root / "bin" / "vix", std::ios::binary) << "executable-not-run";
      }
      return {root, {}};
    }
  };

  struct FakeActivator final : InstallationActivator
  {
    std::optional<std::string> active{"2.3.0"};
    bool failActivation{false};
    int activations{0};
    int rollbacks{0};
    Result<std::optional<std::string>> current_version() override { return {active, {}}; }
    Result<Unit> activate(const std::string &version) override
    {
      ++activations;
      if (failActivation) return {{}, Error{ErrorCode::activation_failed, "injected"}};
      active = version;
      return {Unit{}, {}};
    }
    Result<Unit> rollback(const std::optional<std::string> &version) override
    {
      ++rollbacks;
      active = version;
      return {Unit{}, {}};
    }
  };

  struct FakeExposure final : ExecutableExposure
  {
    int calls{0};
    fs::path received;
    Result<Unit> expose(const fs::path &, const fs::path &activeExecutable) override
    {
      ++calls;
      received = activeExecutable;
      return {Unit{}, {}};
    }
  };

  const std::string secretText =
      "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
      "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
  const std::vector<std::uint8_t> secret = hex_bytes(secretText);
  const std::vector<std::uint8_t> publicKey =
      hex_bytes("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");

  SignedReleaseManifest signed_fixture(const std::string &archiveBytes, const std::string &generation = "current",
                                       std::optional<ReleaseComponent> component = std::nullopt,
                                       vix::cli::project::native::Version releaseVersion = {2, 4, 0, {}})
  {
    const auto host = parse_host_platform("x86_64-linux-gnu");
    const auto digest = digest_distribution_artifact(archiveBytes);
    assert(host.valid() && digest.valid());
    ReleaseManifest manifest;
    manifest.generation = generation;
    manifest.version = std::move(releaseVersion);
    manifest.channel = "stable";
    manifest.sequence = 8;
    manifest.artifacts.push_back({"host-linux", *host.value, "https://releases.invalid/vix.tar.gz",
                                  ArchiveFormat::tar_gz, *digest.value,
                                  static_cast<std::uint64_t>(archiveBytes.size())});
    if (component)
      manifest.components.push_back(std::move(*component));
    const auto encoded = serialize_release_manifest(manifest);
    const auto payload = release_signing_payload(manifest);
    assert(encoded.valid() && payload.valid());
    std::array<std::uint8_t, 64> signature{};
    const auto message = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t *>(payload.value->data()), payload.value->size()};
    assert(vix::crypto::sign(vix::crypto::SignatureAlg::ed25519, secret, message, signature));
    return { *encoded.value, SignatureEnvelope{"ed25519", "runtime-test", vix::crypto::hex_lower(signature)} };
  }

  fs::path make_root(const std::string &label)
  {
    const auto root = fs::temp_directory_path() / ("vix-dist-runtime-" + label);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "versions" / "2.3.0", ec);
    return root;
  }

  fs::path write_receipt(const fs::path &root, InstallKind kind = InstallKind::managed)
  {
    const auto host = parse_host_platform("x86_64-linux-gnu");
    assert(host.valid());
    InstallReceipt receipt{1, "current", {2, 3, 0, {}}, *host.value, "stable", kind, {}, root.generic_string()};
    if (kind == InstallKind::external) receipt.externalOwner = "test-manager";
    const auto bytes = serialize_install_receipt(receipt);
    assert(bytes.valid());
    const auto path = root / "install.toml";
    std::ofstream(path, std::ios::binary) << *bytes.value;
    return path;
  }

  fs::path write_trust(const fs::path &root)
  {
    const auto path = root / "releases.toml";
    std::ofstream output(path, std::ios::binary);
    output << "format = 1\n\n[[trusted-key]]\nkey-id = \"runtime-test\"\nalgorithm = \"ed25519\"\npublic-key = \""
           << vix::crypto::hex_lower(publicKey) << "\"\n";
    return path;
  }

  ReleaseTrustStore trusted_release_store(const fs::path &root)
  {
    const auto loaded = load_release_trust_store(write_trust(root));
    assert(loaded.valid());
    return *loaded.value;
  }

  void component_release_fails_closed(const std::string &label, ReleaseComponent component)
  {
    constexpr const char *archiveBytes = "archive-bytes";
    const auto root = make_root(label);
    const auto receipt = write_receipt(root);
    const auto trust = write_trust(root);
    FakeSource source;
    source.value = signed_fixture(archiveBytes, "current", std::move(component));
    FakeFetcher fetcher;
    FakeExtractor extractor;
    FakeActivator activator;
    UpgradeRuntime runtime{source, fetcher, extractor, activator};
    const auto result = runtime.run(receipt, trust, *parse_host_platform("x86_64-linux-gnu").value);
    assert(!result.valid() && result.error->code == ErrorCode::unsupported_components);
    assert(source.calls == 1 && fetcher.calls == 0 && extractor.calls == 0);
    assert(activator.activations == 0 && activator.active == std::optional<std::string>{"2.3.0"});
    assert(!fs::exists(root / "staging"));
  }

  fs::path write_tar_gz(const fs::path &path, const std::string &name,
                        char type = '0', const std::string &link = {}, const std::string &body = {})
  {
    std::array<char, 512> header{};
    assert(name.size() < 100 && link.size() < 100);
    std::copy(name.begin(), name.end(), header.begin());
    std::copy_n("0000644", 7, header.begin() + 100);
    const auto size = std::snprintf(header.data() + 124, 12, "%011o", static_cast<unsigned>(body.size()));
    assert(size == 11);
    std::fill(header.begin() + 148, header.begin() + 156, ' ');
    header[156] = type;
    std::copy(link.begin(), link.end(), header.begin() + 157);
    std::copy_n("ustar\0", 6, header.begin() + 257);
    unsigned checksum = 0;
    for (const auto byte : header) checksum += static_cast<unsigned char>(byte);
    const auto checksumSize = std::snprintf(header.data() + 148, 7, "%06o", checksum);
    assert(checksumSize == 6);
    header[154] = '\0';
    header[155] = ' ';
    std::string bytes(header.data(), header.size());
    bytes += body;
    bytes.resize(512 + ((body.size() + 511) / 512) * 512, '\0');
    bytes.resize(bytes.size() + 1024, '\0');
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    gzFile output = gzopen(path.string().c_str(), "wb");
    assert(output);
    const int written = gzwrite(output, bytes.data(), static_cast<unsigned>(bytes.size()));
    assert(written == static_cast<int>(bytes.size()));
    assert(gzclose(output) == Z_OK);
    return path;
  }
}

int main()
{
  constexpr const char *archiveBytes = "archive-bytes";
  const auto host = parse_host_platform("x86_64-linux-gnu");
  assert(host.valid());

  {
    const auto root = make_root("verified-first");
    const auto receipt = write_receipt(root);
    const auto trust = write_trust(root);
    FakeSource source;
    source.value = signed_fixture(archiveBytes);
    source.value.signature.signature[0] = source.value.signature.signature[0] == '0' ? '1' : '0';
    FakeFetcher fetcher;
    FakeExtractor extractor;
    FakeActivator activator;
    UpgradeRuntime runtime{source, fetcher, extractor, activator};
    const auto result = runtime.run(receipt, trust, *host.value);
    assert(!result.valid() && result.error->code == ErrorCode::invalid_signature);
    assert(source.calls == 1 && fetcher.calls == 0 && extractor.calls == 0);
  }

  {
    const auto root = make_root("upgrade");
    const auto receipt = write_receipt(root);
    const auto trust = write_trust(root);
    FakeSource source;
    source.value = signed_fixture(archiveBytes);
    FakeFetcher fetcher;
    FakeExtractor extractor;
    FakeActivator activator;
    UpgradeRuntime runtime{source, fetcher, extractor, activator};
    const auto result = runtime.run(receipt, trust, *host.value);
    assert(result.valid() && result.value->kind == RuntimeOutcomeKind::upgraded);
    assert(source.calls == 1 && fetcher.calls == 1 && extractor.calls == 1);
    assert(activator.active == std::optional<std::string>{"2.4.0"});
    assert(fs::is_regular_file(root / "versions" / "2.4.0" / "bin" / "vix"));
    std::ifstream input(receipt, std::ios::binary);
    const std::string receiptBytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    const auto parsed = parse_install_receipt(receiptBytes);
    assert(parsed.valid() && (parsed.value->version == vix::cli::project::native::Version{2, 4, 0, {}}));
  }

  {
    const auto componentHost = parse_host_platform("x86_64-linux-gnu");
    const auto digest = digest_distribution_artifact(archiveBytes);
    assert(componentHost.valid() && digest.valid());
    component_release_fails_closed("host-component",
        ReleaseComponent{"host-tools", ComponentKind::host, *componentHost.value, {},
                         "https://releases.invalid/host-tools.tar.gz", ArchiveFormat::tar_gz,
                         *digest.value, {}, {}});
  }

  {
    const auto target = parse_target_platform("wasm32-wasi");
    const auto digest = digest_distribution_artifact(archiveBytes);
    assert(target.valid() && digest.valid());
    component_release_fails_closed("target-sdk-component",
        ReleaseComponent{"wasm-sdk", ComponentKind::target_sdk, {}, *target.value,
                         "https://releases.invalid/wasm-sdk.tar.gz", ArchiveFormat::tar_gz,
                         *digest.value, {}, {}});
  }

  {
    const auto root = make_root("bad-digest");
    const auto receipt = write_receipt(root);
    const auto trust = write_trust(root);
    FakeSource source;
    source.value = signed_fixture("different-bytes");
    FakeFetcher fetcher;
    FakeExtractor extractor;
    FakeActivator activator;
    UpgradeRuntime runtime{source, fetcher, extractor, activator};
    const auto result = runtime.run(receipt, trust, *host.value);
    assert(!result.valid() && result.error->code == ErrorCode::artifact_digest_mismatch);
    assert(extractor.calls == 0 && activator.activations == 0 && activator.active == std::optional<std::string>{"2.3.0"});
  }

  {
    const auto root = make_root("activation-failure");
    const auto receipt = write_receipt(root);
    const auto trust = write_trust(root);
    FakeSource source;
    source.value = signed_fixture(archiveBytes);
    FakeFetcher fetcher;
    FakeExtractor extractor;
    FakeActivator activator;
    activator.failActivation = true;
    UpgradeRuntime runtime{source, fetcher, extractor, activator};
    const auto result = runtime.run(receipt, trust, *host.value);
    assert(!result.valid() && result.error->code == ErrorCode::activation_failed);
    assert(activator.active == std::optional<std::string>{"2.3.0"} && activator.rollbacks == 1);
  }

  {
    const auto root = make_root("extraction-failure");
    const auto receipt = write_receipt(root);
    const auto trust = write_trust(root);
    FakeSource source;
    source.value = signed_fixture(archiveBytes);
    FakeFetcher fetcher;
    FakeExtractor extractor;
    extractor.fail = true;
    FakeActivator activator;
    UpgradeRuntime runtime{source, fetcher, extractor, activator};
    const auto result = runtime.run(receipt, trust, *host.value);
    assert(!result.valid() && result.error->code == ErrorCode::unsafe_archive);
    assert(activator.activations == 0 && activator.active == std::optional<std::string>{"2.3.0"});
  }

  {
    const auto root = make_root("structure-failure");
    const auto receipt = write_receipt(root);
    const auto trust = write_trust(root);
    FakeSource source;
    source.value = signed_fixture(archiveBytes);
    FakeFetcher fetcher;
    FakeExtractor extractor;
    extractor.omitRequiredStructure = true;
    FakeActivator activator;
    UpgradeRuntime runtime{source, fetcher, extractor, activator};
    const auto result = runtime.run(receipt, trust, *host.value);
    assert(!result.valid() && result.error->code == ErrorCode::invalid_distribution);
    assert(activator.activations == 0 && activator.active == std::optional<std::string>{"2.3.0"});
  }

  {
    const auto root = make_root("wrong-generation");
    const auto receipt = write_receipt(root);
    const auto trust = write_trust(root);
    FakeSource source;
    source.value = signed_fixture(archiveBytes, "vix2");
    FakeFetcher fetcher;
    FakeExtractor extractor;
    FakeActivator activator;
    UpgradeRuntime runtime{source, fetcher, extractor, activator};
    const auto result = runtime.run(receipt, trust, *host.value);
    assert(!result.valid() && result.error->code == ErrorCode::wrong_generation);
    assert(fetcher.calls == 0 && extractor.calls == 0);
  }

  {
    const auto root = make_root("external");
    const auto receipt = write_receipt(root, InstallKind::external);
    const auto trust = write_trust(root);
    FakeSource source;
    FakeFetcher fetcher;
    FakeExtractor extractor;
    FakeActivator activator;
    UpgradeRuntime runtime{source, fetcher, extractor, activator};
    const auto result = runtime.run(receipt, trust, *host.value);
    assert(result.valid() && result.value->kind == RuntimeOutcomeKind::externally_managed);
    assert(source.calls == 0 && fetcher.calls == 0 && extractor.calls == 0);
  }

  {
    const auto root = make_root("archive-owned");
    const auto receipt = write_receipt(root, InstallKind::archive);
    const auto trust = write_trust(root);
    FakeSource source;
    FakeFetcher fetcher;
    FakeExtractor extractor;
    FakeActivator activator;
    UpgradeRuntime runtime{source, fetcher, extractor, activator};
    const auto result = runtime.run(receipt, trust, *host.value);
    assert(result.valid() && result.value->kind == RuntimeOutcomeKind::unmanaged_install);
    assert(source.calls == 0 && fetcher.calls == 0 && extractor.calls == 0);
  }

  SafeTarGzExtractor extractor;
  const auto unsupported = extractor.extract(ArchiveFormat::zip, {}, {});
  assert(!unsupported.valid() && unsupported.error->code == ErrorCode::unsupported_archive);

  const auto archiveRoot = make_root("archive-safety");
  const auto archiveFile = write_tar_gz(archiveRoot / "safe.tar.gz", "bin/vix", '0', {}, "binary");
  const auto safe = extractor.extract(ArchiveFormat::tar_gz, archiveFile, archiveRoot / "safe-output");
  assert(safe.valid() && fs::is_regular_file(*safe.value / "bin" / "vix"));
  const auto traversal = write_tar_gz(archiveRoot / "traversal.tar.gz", "../outside");
  const auto rejectedTraversal = extractor.extract(ArchiveFormat::tar_gz, traversal, archiveRoot / "traversal-output");
  assert(!rejectedTraversal.valid() && rejectedTraversal.error->code == ErrorCode::unsafe_archive);
  const auto absolute = write_tar_gz(archiveRoot / "absolute.tar.gz", "/outside");
  const auto rejectedAbsolute = extractor.extract(ArchiveFormat::tar_gz, absolute, archiveRoot / "absolute-output");
  assert(!rejectedAbsolute.valid() && rejectedAbsolute.error->code == ErrorCode::unsafe_archive);
  const auto symlinkEscape = write_tar_gz(archiveRoot / "symlink.tar.gz", "bin/escape", '2', "../../outside");
  const auto rejectedSymlink = extractor.extract(ArchiveFormat::tar_gz, symlinkEscape, archiveRoot / "symlink-output");
  assert(!rejectedSymlink.valid() && rejectedSymlink.error->code == ErrorCode::unsafe_archive);

#if !defined(_WIN32)
  {
    const auto root = fs::temp_directory_path() / "vix-dist-bootstrap-invalid-host";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const auto trust = trusted_release_store(root);
    FakeSource source;
    FakeFetcher fetcher;
    FakeExtractor packageExtractor;
    PosixInstallationActivator activator{root / "home"};
    FakeExposure exposure;
    ManagedInstallerRuntime runtime{source, fetcher, packageExtractor, activator, exposure};
    const HostPlatform invalidHost{};
    const auto result = runtime.install(root / "home", trust, root / "bin" / "vix", invalidHost);
    assert(!result.valid() && result.error->code == ErrorCode::invalid_value);
    assert(source.calls == 0 && fetcher.calls == 0 && packageExtractor.calls == 0 && exposure.calls == 0);
    assert(!fs::exists(root / "home"));
  }

  {
    const auto root = fs::temp_directory_path() / "vix-dist-bootstrap-success";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const auto trust = trusted_release_store(root);
    FakeSource source;
    source.value = signed_fixture(archiveBytes);
    FakeFetcher fetcher;
    FakeExtractor packageExtractor;
    PosixInstallationActivator activator{root / "home"};
    const auto exposed = root / "user-bin" / "vix";
    PosixExecutableExposure exposure{exposed};
    ManagedInstallerRuntime runtime{source, fetcher, packageExtractor, activator, exposure};
    const auto host = parse_host_platform("x86_64-linux-gnu");
    assert(host.valid());
    const auto home = root / "home";
    const auto result = runtime.install(home, trust, exposed, *host.value);
    assert(result.valid() && result.value->version == "2.4.0");
    assert(source.calls == 1 && fetcher.calls == 1 && packageExtractor.calls == 1);
    assert(fs::is_symlink(exposed));
    assert(fs::read_symlink(exposed) == fs::absolute(home / "current" / "bin" / "vix"));
    const auto active = activator.current_version();
    assert(active.valid() && active.value->value() == "2.4.0");
    std::ifstream receiptInput(home / "install.toml", std::ios::binary);
    const std::string receiptText{std::istreambuf_iterator<char>{receiptInput}, std::istreambuf_iterator<char>{}};
    const auto receipt = parse_install_receipt(receiptText);
    assert(receipt.valid() && receipt.value->installKind == InstallKind::managed &&
           receipt.value->generation == "current" && receipt.value->channel == "stable");
  }

  {
    const auto root = fs::temp_directory_path() / "vix-dist-bootstrap-reject";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const auto trust = trusted_release_store(root);
    FakeSource source;
    source.value = signed_fixture(archiveBytes, "vix2");
    FakeFetcher fetcher;
    FakeExtractor packageExtractor;
    PosixInstallationActivator activator{root / "home"};
    FakeExposure exposure;
    ManagedInstallerRuntime runtime{source, fetcher, packageExtractor, activator, exposure};
    const auto host = parse_host_platform("x86_64-linux-gnu");
    const auto result = runtime.install(root / "home", trust, root / "bin" / "vix", *host.value);
    assert(!result.valid() && result.error->code == ErrorCode::wrong_generation);
    assert(fetcher.calls == 0 && packageExtractor.calls == 0 && exposure.calls == 0);
    const auto active = activator.current_version();
    assert(active.valid() && !active.value->has_value());
  }

  {
    const auto root = fs::temp_directory_path() / "vix-dist-bootstrap-invalid-signature";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const auto trust = trusted_release_store(root);
    FakeSource source;
    source.value = signed_fixture(archiveBytes);
    source.value.signature.signature[0] = source.value.signature.signature[0] == 'A' ? 'B' : 'A';
    FakeFetcher fetcher;
    FakeExtractor packageExtractor;
    PosixInstallationActivator activator{root / "home"};
    FakeExposure exposure;
    ManagedInstallerRuntime runtime{source, fetcher, packageExtractor, activator, exposure};
    const auto host = parse_host_platform("x86_64-linux-gnu");
    const auto result = runtime.install(root / "home", trust, root / "bin" / "vix", *host.value);
    assert(!result.valid() && result.error->code == ErrorCode::invalid_signature);
    assert(fetcher.calls == 0 && packageExtractor.calls == 0 && exposure.calls == 0);
  }

  {
    const auto root = fs::temp_directory_path() / "vix-dist-bootstrap-existing";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "home" / "versions" / "2.3.0", ec);
    const auto trust = trusted_release_store(root);
    FakeSource source;
    source.value = signed_fixture(archiveBytes);
    FakeFetcher fetcher;
    FakeExtractor packageExtractor;
    PosixInstallationActivator activator{root / "home"};
    assert(activator.activate("2.3.0").valid());
    FakeExposure exposure;
    ManagedInstallerRuntime runtime{source, fetcher, packageExtractor, activator, exposure};
    const auto host = parse_host_platform("x86_64-linux-gnu");
    const auto result = runtime.install(root / "home", trust, root / "bin" / "vix", *host.value);
    assert(!result.valid() && result.error->code == ErrorCode::already_installed);
    assert(source.calls == 0 && fetcher.calls == 0 && packageExtractor.calls == 0);
    const auto current = activator.current_version();
    assert(current.valid() && current.value->value() == "2.3.0");
  }

  {
    const auto root = fs::temp_directory_path() / "vix-dist-bootstrap-host-digest";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const auto trust = trusted_release_store(root);
    FakeSource source;
    source.value = signed_fixture(archiveBytes);
    FakeFetcher fetcher;
    FakeExtractor packageExtractor;
    PosixInstallationActivator activator{root / "home"};
    FakeExposure exposure;
    ManagedInstallerRuntime runtime{source, fetcher, packageExtractor, activator, exposure};
    const auto arm = parse_host_platform("aarch64-linux-gnu");
    assert(arm.valid());
    const auto unsupported = runtime.install(root / "home", trust, root / "bin" / "vix", *arm.value);
    assert(!unsupported.valid() && unsupported.error->code == ErrorCode::unsupported_host_artifact);
    assert(fetcher.calls == 0 && packageExtractor.calls == 0);

    source.value = signed_fixture(archiveBytes);
    fetcher.bytes = "different-archive-bytes";
    const auto host = parse_host_platform("x86_64-linux-gnu");
    const auto mismatch = runtime.install(root / "home", trust, root / "bin" / "vix", *host.value);
    assert(!mismatch.valid() && mismatch.error->code == ErrorCode::artifact_digest_mismatch);
    assert(packageExtractor.calls == 0 && exposure.calls == 0);
  }

  {
    const auto root = fs::temp_directory_path() / "vix-dist-bootstrap-unsafe";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const auto trust = trusted_release_store(root);
    FakeSource source;
    source.value = signed_fixture(archiveBytes);
    FakeFetcher fetcher;
    FakeExtractor packageExtractor;
    packageExtractor.fail = true;
    PosixInstallationActivator activator{root / "home"};
    FakeExposure exposure;
    ManagedInstallerRuntime runtime{source, fetcher, packageExtractor, activator, exposure};
    const auto host = parse_host_platform("x86_64-linux-gnu");
    const auto result = runtime.install(root / "home", trust, root / "bin" / "vix", *host.value);
    assert(!result.valid() && result.error->code == ErrorCode::unsafe_archive);
    assert(exposure.calls == 0 && !fs::exists(root / "home" / "current"));
    assert(!fs::exists(root / "home" / "install.toml"));
  }

  {
    const auto root = fs::temp_directory_path() / "vix-dist-bootstrap-upgrade";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const auto trustPath = write_trust(root);
    const auto trust = load_release_trust_store(trustPath);
    assert(trust.valid());
    const auto home = root / "managed";
    const auto host = parse_host_platform("x86_64-linux-gnu");
    FakeSource source;
    source.value = signed_fixture(archiveBytes, "current", {}, {2, 3, 0, {}});
    FakeFetcher fetcher;
    FakeExtractor packageExtractor;
    PosixInstallationActivator activator{home};
    FakeExposure exposure;
    ManagedInstallerRuntime installer{source, fetcher, packageExtractor, activator, exposure};
    const auto installed = installer.install(home, *trust.value, root / "bin" / "vix", *host.value);
    assert(installed.valid() && installed.value->version == "2.3.0");
    assert(fs::is_directory(home / "versions" / "2.3.0"));

    source.value = signed_fixture(archiveBytes);
    UpgradeRuntime upgrader{source, fetcher, packageExtractor, activator};
    const auto upgraded = upgrader.run(home / "install.toml", trustPath, *host.value);
    assert(upgraded.valid() && upgraded.value->kind == RuntimeOutcomeKind::upgraded &&
           upgraded.value->version == std::optional<std::string>{"2.4.0"});
    const auto activeB = activator.current_version();
    assert(activeB.valid() && activeB.value->value() == "2.4.0");
    assert(fs::is_directory(home / "versions" / "2.3.0") && fs::is_directory(home / "versions" / "2.4.0"));
    assert(activator.rollback(std::optional<std::string>{"2.3.0"}).valid());
    const auto rolledBack = activator.current_version();
    assert(rolledBack.valid() && rolledBack.value->value() == "2.3.0");
  }
#endif

#if !defined(_WIN32)
  {
    const auto root = make_root("posix-activation");
    fs::create_directories(root / "versions" / "2.4.0");
    PosixInstallationActivator activator{root};
    assert(activator.activate("2.3.0").valid());
    assert(activator.activate("2.4.0").valid());
    const auto currentNew = activator.current_version();
    assert(currentNew.valid() && currentNew.value->value() == "2.4.0");
    assert(activator.rollback(std::optional<std::string>{"2.3.0"}).valid());
    const auto currentOld = activator.current_version();
    assert(currentOld.valid() && currentOld.value->value() == "2.3.0");
  }
#else
  {
    PosixInstallationActivator activator{"C:/vix"};
    const auto unsupportedWindowsActivation = activator.current_version();
    assert(!unsupportedWindowsActivation.valid() &&
           unsupportedWindowsActivation.error->code == ErrorCode::activation_failed);
  }
#endif
}
