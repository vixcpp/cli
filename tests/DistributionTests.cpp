#include <vix/cli/distribution/Distribution.hpp>
#include <vix/cli/distribution/Runtime.hpp>
#include <vix/crypto/hex.hpp>
#include <vix/crypto/signature.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <span>
#include <vector>

using namespace vix::cli::distribution;

namespace
{
  std::vector<std::uint8_t> hex_bytes(const std::string &text)
  {
    std::vector<std::uint8_t> out(text.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i)
    {
      const auto d = [](char c) -> unsigned { return c <= '9' ? unsigned(c - '0') : unsigned(c - 'a' + 10); };
      out[i] = static_cast<std::uint8_t>((d(text[i * 2]) << 4) | d(text[i * 2 + 1]));
    }
    return out;
  }

  ReleaseManifest fixture_manifest()
  {
    const auto host = parse_host_platform("x86_64-linux-gnu");
    const auto digest = DistributionArtifactDigest::parse("sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(host.valid() && digest.valid());
    ReleaseManifest manifest;
    manifest.generation = "current";
    manifest.version = {2, 4, 0, {}};
    manifest.channel = "stable";
    manifest.sequence = 17;
    manifest.artifacts.push_back({"cli-linux", *host.value, "https://dist.invalid/vix.tar.gz", ArchiveFormat::tar_gz, *digest.value, 42});
    auto wasm = parse_target_platform("wasm32-wasi");
    assert(wasm.valid());
    manifest.components.push_back({"wasi-sdk", ComponentKind::target_sdk, {}, *wasm.value,
                                   "https://dist.invalid/wasi.zip", ArchiveFormat::zip, *digest.value, {}});
    manifest.components.push_back({"bash-completion", ComponentKind::platform_independent, {}, {},
                                   "https://dist.invalid/completions.zip", ArchiveFormat::zip, *digest.value, {},
                                   "share/completions/bash/vix"});
    manifest.components.push_back({"zsh-completion", ComponentKind::platform_independent, {}, {},
                                   "https://dist.invalid/completions.zip", ArchiveFormat::zip, *digest.value, {},
                                   "share/completions/zsh/_vix"});
    manifest.components.push_back({"fish-completion", ComponentKind::platform_independent, {}, {},
                                   "https://dist.invalid/completions.zip", ArchiveFormat::zip, *digest.value, {},
                                   "share/completions/fish/vix.fish"});
    return manifest;
  }
}

int main()
{
  const auto sourceConfig = parse_distribution_source_config(
      "format = 1\n[channels.stable]\nmanifest = \"https://updates.invalid/releases.toml\"\nsignature = \"https://updates.invalid/releases.sig\"\n");
  assert(sourceConfig.valid() && sourceConfig.value->channels.contains("stable"));
  assert(!parse_distribution_source_config(
      "format = 1\n[channels.stable]\nmanifest = \"http://updates.invalid/releases.toml\"\nsignature = \"https://updates.invalid/releases.sig\"\n").valid());
  HttpsReleaseManifestSource missingSource{*sourceConfig.value};
  const auto missingChannel = missingSource.fetch("beta");
  assert(!missingChannel.valid() && missingChannel.error->code == ErrorCode::release_source_missing);
  SafeTarGzExtractor safeExtractor;
  const auto unsupportedZip = safeExtractor.extract(ArchiveFormat::zip, {}, {});
  assert(!unsupportedZip.valid() && unsupportedZip.error->code == ErrorCode::unsupported_archive);

  for (const auto &tuple : {"x86_64-linux-gnu", "aarch64-linux-gnu", "x86_64-windows-msvc",
                            "aarch64-windows-msvc", "x86_64-macos", "aarch64-macos"})
  {
    const auto parsed = parse_host_platform(tuple);
    assert(parsed.valid() && parsed.value->canonical() == tuple);
  }
  const auto x86 = parse_host_platform("x86_64-linux-gnu");
  const auto arm = parse_host_platform("aarch64-linux-gnu");
  assert(x86.valid() && arm.valid() && !(*x86.value == *arm.value));
  assert(!parse_host_platform("wasm32-wasi").valid());
  const auto wasm = parse_target_platform("wasm32-wasi");
  assert(wasm.valid() && wasm.value->triple == "wasm32-wasi");

  const auto digest = digest_distribution_artifact("abc");
  assert(digest.valid() && digest.value->value == "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  assert(verify_distribution_artifact("abc", *digest.value).valid());
  assert(!verify_distribution_artifact("abd", *digest.value).valid());
  assert(!DistributionArtifactDigest::parse("sha256:BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD").valid());

  auto manifest = fixture_manifest();
  const auto generatedManifest = make_current_release_manifest(
      {2, 4, 0, {}}, "stable", 17, manifest.artifacts);
  assert(generatedManifest.valid() && generatedManifest.value->generation == "current" &&
         generatedManifest.value->components.empty());
  const auto generatedBytes = serialize_release_manifest(*generatedManifest.value);
  const auto generatedBytesAgain = serialize_release_manifest(*generatedManifest.value);
  assert(generatedBytes.valid() && generatedBytesAgain.valid() &&
         *generatedBytes.value == *generatedBytesAgain.value);
  const auto encoded = serialize_release_manifest(manifest);
  assert(encoded.valid());
  const auto decoded = parse_release_manifest(*encoded.value);
  assert(decoded.valid());
  const auto canonicalAgain = serialize_release_manifest(*decoded.value);
  assert(canonicalAgain.valid() && *canonicalAgain.value == *encoded.value);
  assert(encoded.value->find("install-path = \"share/completions/bash/vix\"") != std::string::npos);
  auto badDigestText = *encoded.value;
  const auto digestPosition = badDigestText.find("sha256:");
  assert(digestPosition != std::string::npos);
  badDigestText[digestPosition + 7] = 'A';
  const auto badManifestDigest = parse_release_manifest(badDigestText);
  assert(!badManifestDigest.valid() && badManifestDigest.error->code == ErrorCode::invalid_artifact_digest);
  const auto payload1 = release_signing_payload(manifest);
  const auto payload2 = release_signing_payload(manifest);
  assert(payload1.valid() && payload1.value->starts_with("VIX-RELEASE-MANIFEST-v1\n") && payload1.value == payload2.value);

  // RFC 8032 test keypair; signing is deterministic and confined to this test.
  const auto secret = hex_bytes("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
                               "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  const auto publicKey = hex_bytes("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  const auto signedEnvelope = sign_release_manifest(manifest, "release-test", secret);
  assert(signedEnvelope.valid());
  ReleaseTrustStore trust{{ReleaseTrustKey{"release-test", "ed25519", vix::crypto::hex_lower(publicKey)}}};
  SignatureEnvelope envelope = *signedEnvelope.value;
  const auto verifiedManifest = verify_release_manifest(*encoded.value, envelope, trust);
  assert(verifiedManifest.valid());
  auto altered = manifest;
  altered.version = {2, 5, 0, {}};
  const auto alteredText = serialize_release_manifest(altered);
  assert(alteredText.valid() && !verify_release_manifest(*alteredText.value, envelope, trust).valid());
  envelope.keyId = "unknown";
  const auto unknown = verify_release_manifest(*encoded.value, envelope, trust);
  assert(!unknown.valid() && unknown.error->code == ErrorCode::unknown_key);
  envelope.keyId = "release-test";
  auto invalidSignature = envelope;
  invalidSignature.signature[0] = invalidSignature.signature[0] == '0' ? '1' : '0';
  const auto invalid = verify_release_manifest(*encoded.value, invalidSignature, trust);
  assert(!invalid.valid() && invalid.error->code == ErrorCode::invalid_signature);
  auto unsupportedAlgorithm = envelope;
  unsupportedAlgorithm.algorithm = "rsa";
  const auto unsupportedSignature = verify_release_manifest(*encoded.value, unsupportedAlgorithm, trust);
  assert(!unsupportedSignature.valid() && unsupportedSignature.error->code == ErrorCode::unsupported_algorithm);
  const auto wrongGeneration = verify_release_manifest(*encoded.value, envelope, trust, "vix2");
  assert(!wrongGeneration.valid() && wrongGeneration.error->code == ErrorCode::wrong_generation);
  const auto envelopeText = serialize_signature_envelope(envelope);
  assert(envelopeText.valid() && parse_signature_envelope(*envelopeText.value).valid());
  const auto trustText = std::string{"format = 1\n\n[[trusted-key]]\nkey-id = \"release-test\"\nalgorithm = \"ed25519\"\npublic-key = \"" +
                               vix::crypto::hex_lower(publicKey) + "\"\n"};
  assert(parse_release_trust_store(trustText).valid());

  InstallReceipt receipt{1, "current", {2, 3, 0, {}}, *x86.value, "stable", InstallKind::managed, {}, "/relocated/vix"};
  const auto receiptText = serialize_install_receipt(receipt);
  assert(receiptText.valid());
  const auto receiptRoundTrip = parse_install_receipt(*receiptText.value);
  assert(receiptRoundTrip.valid() && receiptRoundTrip.value->installationRoot == receipt.installationRoot);
  const auto plan = plan_upgrade(receipt, *verifiedManifest.value, *x86.value);
  assert(plan.valid() && plan.value->kind == PlanKind::upgrade_available && plan.value->artifact->host == *x86.value);
  auto external = receipt;
  external.installKind = InstallKind::external;
  external.externalOwner = "homebrew";
  const auto externalPlan = plan_upgrade(external, *verifiedManifest.value, *x86.value);
  assert(externalPlan.valid() && externalPlan.value->kind == PlanKind::externally_managed && externalPlan.value->externalOwner == "homebrew");
  auto archived = receipt;
  archived.installKind = InstallKind::archive;
  const auto archivePlan = plan_upgrade(archived, *verifiedManifest.value, *x86.value);
  assert(archivePlan.valid() && archivePlan.value->kind == PlanKind::archive_installation);
  const auto wrongArch = parse_host_platform("aarch64-linux-gnu");
  assert(wrongArch.valid());
  const auto unsupported = plan_upgrade(receipt, *verifiedManifest.value, *wrongArch.value);
  assert(unsupported.valid() && unsupported.value->kind == PlanKind::unsupported_host);
  auto same = manifest;
  same.version = receipt.version;
  const auto samePayload = release_signing_payload(same);
  std::array<std::uint8_t, 64> sameSignature{};
  const auto sameMessage = std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t *>(samePayload.value->data()), samePayload.value->size()};
  assert(vix::crypto::sign(vix::crypto::SignatureAlg::ed25519, secret, sameMessage, sameSignature));
  const SignatureEnvelope sameEnvelope{"ed25519", "release-test", vix::crypto::hex_lower(sameSignature)};
  const auto sameVerified = verify_release_manifest(*serialize_release_manifest(same).value, sameEnvelope, trust);
  assert(sameVerified.valid() && plan_upgrade(receipt, *sameVerified.value, *x86.value).value->kind == PlanKind::already_current);
  auto vix2Manifest = manifest;
  vix2Manifest.generation = "vix2";
  const auto vix2Payload = release_signing_payload(vix2Manifest);
  std::array<std::uint8_t, 64> vix2Signature{};
  const auto vix2Message = std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t *>(vix2Payload.value->data()), vix2Payload.value->size()};
  assert(vix::crypto::sign(vix::crypto::SignatureAlg::ed25519, secret, vix2Message, vix2Signature));
  const SignatureEnvelope vix2Envelope{"ed25519", "release-test", vix::crypto::hex_lower(vix2Signature)};
  const auto vix2Verified = verify_release_manifest(*serialize_release_manifest(vix2Manifest).value, vix2Envelope, trust, "vix2");
  assert(vix2Verified.valid() && plan_upgrade(receipt, *vix2Verified.value, *x86.value).value->kind == PlanKind::wrong_generation);

  const auto layout = installation_layout("/relocatable/root", "2.4.0");
  assert(layout.valid() && layout.value->versionRoot.generic_string() == "/relocatable/root/versions/2.4.0");
  const auto trustPath = release_trust_store_path("/relocatable/root");
  assert(trustPath.valid() && trustPath.value->generic_string() == "/relocatable/root/share/vix/trust/releases.toml");
  const auto currentHome = distribution_home("/opt/tools", "current");
  const auto legacyHome = distribution_home("/opt/tools", "vix2");
  assert(currentHome.valid() && legacyHome.valid() && *currentHome.value != *legacyHome.value);
}
