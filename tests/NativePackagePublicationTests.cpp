#include <vix/cli/project/native/PackagePublication.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using namespace vix::cli::project::native;
namespace fs = std::filesystem;

namespace
{
  class FakePublicationTransport final : public PackagePublicationTransport
  {
  public:
    PublicationTransportStatus response{PublicationTransportStatus::accepted};
    int calls{0};
    std::string endpoint;
    std::string token;
    std::optional<PackagePublicationSubmission> submission;

    PublicationTransportResult submit(const std::string &url, const std::string &bearer,
      const PackagePublicationSubmission &value) override
    {
      ++calls;
      endpoint = url;
      token = bearer;
      submission = value;
      return {response, {}};
    }
  };

  struct Fixture
  {
    fs::path root;
    Fixture()
    {
      const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      root = fs::temp_directory_path() / ("vix-publication-test-" + std::to_string(stamp));
      fs::create_directories(root / "include");
      fs::create_directories(root / ".git" / "objects");
      fs::create_directories(root / ".vix" / "state");
      write("vix.package.toml",
        "format = 1\n\n[package]\nsource = \"registry\"\nregistry = \"vix\"\n"
        "publisher = \"acme\"\nname = \"widget\"\nversion = \"1.2.3\"\n\n"
        "[integration]\ntype = \"native-vix\"\ninclude-roots = []\n\n"
        "[[exports]]\nname = \"widget\"\nproject-target = \"widget\"\n");
      write("include/widget.hpp", "#pragma once\n");
      write(".git/objects/secret", "git state");
      write(".vix/state/local", "local state");
      write("vix.toml", "project = \"must not change\"\n");
      write("vix.lock", "lock bytes stay exact\n");
    }
    ~Fixture() { std::error_code ignored; fs::remove_all(root, ignored); }
    void write(const fs::path &relative, const std::string &bytes)
    {
      std::ofstream output(root / relative, std::ios::binary);
      output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    std::string read(const fs::path &relative) const
    {
      std::ifstream input(root / relative, std::ios::binary);
      return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }
  };

  void preparation_uses_p10o_and_excludes_local_state()
  {
    Fixture fixture;
    const auto result = prepare_package_publication(fixture.root);
    assert(result.valid() && result.submission);
    assert(result.submission->identity == (RegistryPackageIdentity{"vix", "acme", "widget"}));
    assert(result.submission->version == (Version{1, 2, 3, std::nullopt}));
    assert(result.submission->contentId.digest().starts_with("sha256:"));
    const auto parsed = parse_canonical_package_content(result.submission->canonicalContent);
    assert(parsed.valid() && parsed.contentId == result.submission->contentId);
    assert(parsed.tree->entries.size() == 4);
    for (const auto &entry : parsed.tree->entries)
      assert(!entry.path.starts_with(".git/") && !entry.path.starts_with(".vix/"));
    const auto contract = parse_package_contract_text(result.submission->contractToml);
    assert(contract.valid() && contract.value);
    const auto *registry = std::get_if<RegistryPackageContractIdentity>(&contract.value->identity);
    assert(registry && registry->identity == result.submission->identity &&
      registry->version == result.submission->version);
    assert(std::holds_alternative<NativeVixIntegration>(contract.value->integration));
    assert(contract.value->exports.size() == 1 && contract.value->exports.front().name == "widget");
  }

  void submission_status_and_state_immutability()
  {
    Fixture fixture;
    const auto manifest = fixture.read("vix.toml");
    const auto lock = fixture.read("vix.lock");
    FakePublicationTransport transport;
    RegistryPublicationDirectory directory{{
      {"vix", "https://registry.example/v1", "VIX_REGISTRY_TOKEN"},
      {"acme", "https://packages.acme.example/v1", "ACME_VIX_TOKEN"}}};
    std::string requestedTokenVariable;
    const auto tokenLookup = [&](std::string_view name) -> std::optional<std::string>
    { requestedTokenVariable = name; return "secret-token"; };
    const auto accepted = submit_package_publication(fixture.root, directory, tokenLookup, transport);
    assert(accepted.valid() && transport.calls == 1);
    assert(transport.endpoint == "https://registry.example/v1" && transport.token == "secret-token");
    assert(requestedTokenVariable == "VIX_REGISTRY_TOKEN");
    assert(transport.submission && transport.submission->contentId == accepted.submission->contentId);
    assert(fixture.read("vix.toml") == manifest && fixture.read("vix.lock") == lock);
    assert(fixture.read("vix.package.toml") ==
      "format = 1\n\n[package]\nsource = \"registry\"\nregistry = \"vix\"\n"
      "publisher = \"acme\"\nname = \"widget\"\nversion = \"1.2.3\"\n\n"
      "[integration]\ntype = \"native-vix\"\ninclude-roots = []\n\n"
      "[[exports]]\nname = \"widget\"\nproject-target = \"widget\"\n");

    transport.response = PublicationTransportStatus::already_exists_same_content;
    assert(submit_package_publication(fixture.root, directory, tokenLookup, transport).valid());
    transport.response = PublicationTransportStatus::unauthorized;
    const auto unauthorized = submit_package_publication(fixture.root, directory, tokenLookup, transport);
    assert(unauthorized.code == PackagePublicationErrorCode::unauthorized);
    assert(unauthorized.diagnostic.find("secret-token") == std::string::npos);
    transport.response = PublicationTransportStatus::version_conflict;
    assert(submit_package_publication(fixture.root, directory, tokenLookup, transport).code ==
      PackagePublicationErrorCode::version_conflict);
    transport.response = PublicationTransportStatus::content_mismatch;
    assert(submit_package_publication(fixture.root, directory, tokenLookup, transport).code ==
      PackagePublicationErrorCode::content_mismatch);
    transport.response = PublicationTransportStatus::transport_error;
    assert(submit_package_publication(fixture.root, directory, tokenLookup, transport).code ==
      PackagePublicationErrorCode::transport_error);
    assert(fixture.read("vix.toml") == manifest && fixture.read("vix.lock") == lock);
  }

  void preflight_failures_do_not_submit()
  {
    Fixture fixture;
    FakePublicationTransport transport;
    const auto missingConfig = load_registry_publication_directory(fixture.root / ".vix/missing.toml");
    assert(!missingConfig.valid() && missingConfig.code == PackagePublicationErrorCode::publication_config_unavailable);
    RegistryPublicationDirectory wrongRegistry{{
      {"acme", "https://packages.acme.example/v1", "ACME_VIX_TOKEN"}}};
    const auto noTarget = submit_package_publication(fixture.root, wrongRegistry,
      [](std::string_view) -> std::optional<std::string> { return "token"; }, transport);
    assert(noTarget.code == PackagePublicationErrorCode::publication_target_not_configured);
    RegistryPublicationDirectory directory{{
      {"vix", "https://registry.example/v1", "VIX_REGISTRY_TOKEN"}}};
    const auto noCredential = submit_package_publication(fixture.root, directory,
      [](std::string_view) -> std::optional<std::string> { return std::nullopt; }, transport);
    assert(noCredential.code == PackagePublicationErrorCode::credential_missing);
    fs::remove(fixture.root / "vix.package.toml");
    assert(submit_package_publication(fixture.root, directory,
      [](std::string_view) -> std::optional<std::string> { return "token"; }, transport).code ==
      PackagePublicationErrorCode::package_manifest_missing);
    assert(transport.calls == 0);
  }

  void publication_config_is_generic_and_uses_environment_indirection()
  {
    Fixture fixture;
    fixture.write(".vix/publishing.toml",
      "format = 1\n\n[registries.vix]\nendpoint = \"https://registry.example/v1\"\n"
      "token_env = \"VIX_REGISTRY_TOKEN\"\n\n[registries.acme]\n"
      "endpoint = \"https://packages.acme.example/v1\"\ntoken_env = \"ACME_VIX_TOKEN\"\n");
    const auto loaded = load_registry_publication_directory(fixture.root / ".vix/publishing.toml");
    assert(loaded.valid());
    assert(loaded.directory->find("vix") != nullptr);
    assert(loaded.directory->find("acme") != nullptr);
    assert(loaded.directory->find("community") == nullptr);
    assert(loaded.directory->find("vix")->endpoint == "https://registry.example/v1");
    assert(loaded.directory->find("acme")->tokenEnv == "ACME_VIX_TOKEN");

    fixture.write("vix.package.toml",
      "format = 1\n\n[package]\nsource = \"registry\"\nregistry = \"acme\"\n"
      "publisher = \"acme\"\nname = \"widget\"\nversion = \"1.2.3\"\n\n"
      "[integration]\ntype = \"native-vix\"\ninclude-roots = []\n\n"
      "[[exports]]\nname = \"widget\"\nproject-target = \"widget\"\n");
    FakePublicationTransport transport;
    std::string requested;
    const auto result = submit_package_publication(fixture.root, *loaded.directory,
      [&](std::string_view name) -> std::optional<std::string>
      { requested = name; return "acme-token"; }, transport);
    assert(result.valid());
    assert(result.submission->identity.registry == "acme");
    assert(transport.endpoint == "https://packages.acme.example/v1");
    assert(transport.token == "acme-token" && requested == "ACME_VIX_TOKEN");
  }

  void git_only_contract_is_not_publishable()
  {
    Fixture fixture;
    fixture.write("vix.package.toml",
      "format = 1\n\n[package]\nsource = \"git\"\n\n[integration]\n"
      "type = \"native-vix\"\ninclude-roots = []\n\n[[exports]]\n"
      "name = \"widget\"\nproject-target = \"widget\"\n");
    const auto result = prepare_package_publication(fixture.root);
    assert(!result.valid() && result.code == PackagePublicationErrorCode::non_registry_package);
  }
}

int main()
{
  preparation_uses_p10o_and_excludes_local_state();
  submission_status_and_state_immutability();
  preflight_failures_do_not_submit();
  publication_config_is_generic_and_uses_environment_indirection();
  git_only_contract_is_not_publishable();
  return 0;
}
