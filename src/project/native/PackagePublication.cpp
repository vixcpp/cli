#include <vix/cli/project/native/PackagePublication.hpp>
#include <vix/env/Legacy.hpp>

#include <toml.hpp>
#include <vix/requests/Body.hpp>
#include <vix/requests/Client.hpp>
#include <vix/requests/RequestOptions.hpp>
#include <vix/requests/Url.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>
#include <system_error>

namespace vix::cli::project::native
{
  namespace fs = std::filesystem;
  namespace
  {
    using json = nlohmann::json;

    PackagePublicationResult failure(PackagePublicationErrorCode code, std::string message,
      std::optional<PackagePublicationSubmission> submission = std::nullopt)
    { return {false, code, std::move(submission), std::move(message)}; }

    std::string version_text(const Version &version)
    {
      auto text = std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
        std::to_string(version.patch);
      if (version.prerelease) text += "-" + *version.prerelease;
      return text;
    }

    bool valid_endpoint(const std::string &endpoint)
    {
      try
      {
        const auto parsed = vix::requests::Url::parse(endpoint);
        if (!parsed.is_https() || parsed.host().empty() || !parsed.query().empty() || !parsed.fragment().empty())
          return false;
        const auto schemeEnd = endpoint.find("://");
        if (schemeEnd == std::string::npos) return false;
        const auto authorityStart = schemeEnd + 3;
        const auto authorityEnd = endpoint.find_first_of("/?#", authorityStart);
        const auto authority = endpoint.substr(authorityStart,
          authorityEnd == std::string::npos ? std::string::npos : authorityEnd - authorityStart);
        return authority.find('@') == std::string::npos;
      }
      catch (...) { return false; }
    }

    std::string base64(std::string_view bytes)
    {
      constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      std::string result;
      result.reserve(((bytes.size() + 2) / 3) * 4);
      for (std::size_t i = 0; i < bytes.size(); i += 3)
      {
        const auto remaining = bytes.size() - i;
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = remaining > 1 ? static_cast<unsigned char>(bytes[i + 1]) : 0U;
        const auto c = remaining > 2 ? static_cast<unsigned char>(bytes[i + 2]) : 0U;
        const auto bits = (static_cast<unsigned int>(a) << 16U) |
          (static_cast<unsigned int>(b) << 8U) | static_cast<unsigned int>(c);
        result.push_back(alphabet[(bits >> 18U) & 63U]);
        result.push_back(alphabet[(bits >> 12U) & 63U]);
        result.push_back(remaining > 1 ? alphabet[(bits >> 6U) & 63U] : '=');
        result.push_back(remaining > 2 ? alphabet[bits & 63U] : '=');
      }
      return result;
    }

    std::optional<json> response_json(std::string_view body)
    {
      try { return json::parse(body); }
      catch (...) { return std::nullopt; }
    }

    PublicationTransportStatus response_status(std::string_view status)
    {
      if (status == "accepted") return PublicationTransportStatus::accepted;
      if (status == "already_exists_same_content") return PublicationTransportStatus::already_exists_same_content;
      if (status == "unauthorized") return PublicationTransportStatus::unauthorized;
      if (status == "version_conflict") return PublicationTransportStatus::version_conflict;
      if (status == "invalid_package") return PublicationTransportStatus::invalid_package;
      if (status == "content_mismatch") return PublicationTransportStatus::content_mismatch;
      return PublicationTransportStatus::registry_error;
    }

    PublicationTransportStatus http_status(int status)
    {
      if (status == 401 || status == 403) return PublicationTransportStatus::unauthorized;
      if (status == 409) return PublicationTransportStatus::version_conflict;
      if (status == 400 || status == 422) return PublicationTransportStatus::invalid_package;
      if (status >= 500) return PublicationTransportStatus::registry_error;
      return PublicationTransportStatus::transport_error;
    }

    PublicationTransportResult submission_result(PublicationTransportStatus status, std::string message = {})
    { return {status, std::move(message)}; }

    std::optional<std::string> env_value(const char *name)
    {
      const char *value = vix::env::legacy::getenv(name);
      if (!value || !*value) return std::nullopt;
      return std::string{value};
    }

    bool valid_token_environment_name(std::string_view name)
    {
      if (name.empty() || !((name.front() >= 'A' && name.front() <= 'Z') ||
          (name.front() >= 'a' && name.front() <= 'z') || name.front() == '_')) return false;
      return std::all_of(name.begin() + 1, name.end(), [](unsigned char value)
      { return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
          (value >= '0' && value <= '9') || value == '_'; });
    }

    void strict_table_keys(const toml::table &table, std::initializer_list<std::string_view> allowed,
      std::string_view owner)
    {
      for (const auto &[key, value] : table)
      {
        (void)value;
        const auto name = std::string_view{key.str()};
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
          throw std::runtime_error("unknown " + std::string{owner} + " field: " + std::string{name});
      }
    }

    bool excluded_path(const fs::path &relative)
    {
      for (const auto &component : relative)
        if (component == ".git" || component == ".vix") return true;
      return false;
    }

    std::string path_utf8(const fs::path &path)
    {
#if defined(__cpp_lib_char8_t)
      const auto value = path.generic_u8string();
      return {reinterpret_cast<const char *>(value.data()), value.size()};
#else
      return path.generic_u8string();
#endif
    }

    std::string read_file_bytes(const fs::path &path)
    {
      std::ifstream input(path, std::ios::binary);
      if (!input) throw std::runtime_error("cannot open package file " + path.filename().string());
      std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
      if (input.bad()) throw std::runtime_error("cannot read package file " + path.filename().string());
      return bytes;
    }

    bool executable_file(const fs::file_status &status)
    {
      const auto permissions = status.permissions();
      using P = fs::perms;
      return (permissions & (P::owner_exec | P::group_exec | P::others_exec)) != P::none;
    }

    PackagePublicationResult collect_tree(const fs::path &root, RegistryPackageIdentity identity,
      Version version, PackageContract contract, std::string contractToml)
    {
      try
      {
        CanonicalPackageTree tree;
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::none, ec), end;
        if (ec) return failure(PackagePublicationErrorCode::content_collection_failed,
          "cannot enumerate package root: " + ec.message());
        for (; it != end; it.increment(ec))
        {
          if (ec) return failure(PackagePublicationErrorCode::content_collection_failed,
            "cannot enumerate package tree: " + ec.message());
          const auto relative = it->path().lexically_relative(root);
          if (excluded_path(relative))
          {
            if (it->is_directory(ec) && !ec) it.disable_recursion_pending();
            ec.clear();
            continue;
          }
          const auto status = it->symlink_status(ec);
          if (ec) return failure(PackagePublicationErrorCode::content_collection_failed,
            "cannot inspect package entry " + path_utf8(relative) + ": " + ec.message());
          CanonicalPackageEntry entry;
          entry.path = path_utf8(relative);
          if (fs::is_symlink(status))
          {
            entry.kind = CanonicalPackageEntryKind::symlink;
            entry.target = path_utf8(fs::read_symlink(it->path(), ec));
            if (ec) return failure(PackagePublicationErrorCode::content_collection_failed,
              "cannot read package symlink " + entry.path + ": " + ec.message());
          }
          else if (fs::is_regular_file(status))
          {
            entry.kind = CanonicalPackageEntryKind::file;
            entry.executable = executable_file(status);
            const auto bytes = read_file_bytes(it->path());
            entry.content.reserve(bytes.size());
            for (const char character : bytes)
            {
              const auto byte = static_cast<unsigned char>(character);
              entry.content.push_back(static_cast<std::byte>(byte));
            }
          }
          else if (!fs::is_directory(status))
          {
            return failure(PackagePublicationErrorCode::content_collection_failed,
              "unsupported filesystem entry in package: " + entry.path);
          }
          if (!fs::is_directory(status)) tree.entries.push_back(std::move(entry));
        }
        const auto canonical = canonicalize_package_content_tree(tree);
        if (!canonical.valid())
          return failure(PackagePublicationErrorCode::content_collection_failed,
            canonical.issues.empty() ? "package tree failed P10O validation" : canonical.issues.front().message);
        PackagePublicationSubmission submission{std::move(identity), std::move(version), std::move(contract),
          *canonical.contentId, std::move(contractToml), *canonical.canonicalBytes};
        return {true, PackagePublicationErrorCode::none, std::move(submission), {}};
      }
      catch (const std::exception &error)
      {
        return failure(PackagePublicationErrorCode::content_collection_failed, error.what());
      }
    }

    PackagePublicationErrorCode command_error(PublicationTransportStatus status)
    {
      switch (status)
      {
      case PublicationTransportStatus::accepted:
      case PublicationTransportStatus::already_exists_same_content: return PackagePublicationErrorCode::none;
      case PublicationTransportStatus::unauthorized: return PackagePublicationErrorCode::unauthorized;
      case PublicationTransportStatus::version_conflict: return PackagePublicationErrorCode::version_conflict;
      case PublicationTransportStatus::invalid_package: return PackagePublicationErrorCode::invalid_package;
      case PublicationTransportStatus::content_mismatch: return PackagePublicationErrorCode::content_mismatch;
      case PublicationTransportStatus::registry_error: return PackagePublicationErrorCode::registry_error;
      case PublicationTransportStatus::transport_error: return PackagePublicationErrorCode::transport_error;
      }
      return PackagePublicationErrorCode::transport_error;
    }

    std::string publication_status_message(PublicationTransportStatus status)
    {
      switch (status)
      {
      case PublicationTransportStatus::accepted: return "publication accepted";
      case PublicationTransportStatus::already_exists_same_content: return "identical package version already exists";
      case PublicationTransportStatus::unauthorized: return "Registry rejected the publisher credentials";
      case PublicationTransportStatus::version_conflict: return "package version already exists with different content";
      case PublicationTransportStatus::invalid_package: return "Registry rejected the package contract";
      case PublicationTransportStatus::content_mismatch: return "Registry recomputed a different package content digest";
      case PublicationTransportStatus::registry_error: return "Registry publication service failed";
      case PublicationTransportStatus::transport_error: return "publication transport failed";
      }
      return "publication failed";
    }

    PackagePublicationResult submit_prepared(PackagePublicationResult prepared,
      const PublicationTarget &target, const std::string &bearerToken,
      PackagePublicationTransport &transport)
    {
      if (!prepared.valid() || !prepared.submission) return prepared;
      if (target.registryId != prepared.submission->identity.registry)
        return failure(PackagePublicationErrorCode::publication_target_not_configured,
          "publication target does not match package RegistryId", std::move(prepared.submission));
      if (!valid_endpoint(target.endpoint))
        return failure(PackagePublicationErrorCode::invalid_endpoint,
          "configured publication endpoint must be a valid HTTPS base URL", std::move(prepared.submission));
      if (bearerToken.empty())
        return failure(PackagePublicationErrorCode::credential_missing,
          "credential environment variable " + target.tokenEnv + " is not set", std::move(prepared.submission));
      const auto response = transport.submit(target.endpoint, bearerToken, *prepared.submission);
      if (response.status == PublicationTransportStatus::accepted ||
          response.status == PublicationTransportStatus::already_exists_same_content)
      {
        prepared.succeeded = true;
        prepared.code = PackagePublicationErrorCode::none;
        prepared.diagnostic = publication_status_message(response.status);
        return prepared;
      }
      const auto code = command_error(response.status);
      return failure(code, publication_status_message(response.status), std::move(prepared.submission));
    }
  }

  PackagePublicationResult prepare_package_publication(const fs::path &packageRoot)
  {
    std::error_code ec;
    const auto root = fs::canonical(packageRoot, ec);
    if (ec || !fs::is_directory(root))
      return failure(PackagePublicationErrorCode::content_collection_failed,
        "package root is not an accessible directory");
    const auto manifestPath = root / "vix.package.toml";
    const auto manifestStatus = fs::symlink_status(manifestPath, ec);
    if (ec || !fs::is_regular_file(manifestStatus))
      return failure(PackagePublicationErrorCode::package_manifest_missing,
        "vix.package.toml must be a regular file in the package root");
    const auto parsed = parse_package_contract_file(manifestPath);
    if (!parsed.valid() || !parsed.value)
      return failure(PackagePublicationErrorCode::package_manifest_invalid,
        parsed.issues.empty() ? "vix.package.toml could not be parsed" : parsed.issues.front().message);
    const auto &contract = *parsed.value;
    const auto validation = validate_package_contract(contract);
    if (!validation.valid())
      return failure(PackagePublicationErrorCode::invalid_package_contract,
        validation.issues.empty() ? "package contract is invalid" : validation.issues.front().message);
    const auto *registry = std::get_if<RegistryPackageContractIdentity>(&contract.identity);
    if (!registry)
      return failure(PackagePublicationErrorCode::non_registry_package,
        "Git-source package contracts do not declare a Registry identity and cannot be published");
    const auto pathValidation = validate_package_contract_paths(contract, root);
    if (!pathValidation.valid())
      return failure(PackagePublicationErrorCode::invalid_package_contract,
        pathValidation.issues.empty() ? "package contract paths are invalid" : pathValidation.issues.front().message);
    const auto serializedContract = serialize_package_contract_text(contract);
    if (!serializedContract.valid() || !serializedContract.text)
      return failure(PackagePublicationErrorCode::invalid_package_contract,
        "package contract serialization failed");
    return collect_tree(root, registry->identity, registry->version, contract, *serializedContract.text);
  }

  PublicationTransportResult RequestsPackagePublicationTransport::submit(
    const std::string &endpoint, const std::string &bearerToken,
    const PackagePublicationSubmission &submission)
  {
    if (!valid_endpoint(endpoint))
      return submission_result(PublicationTransportStatus::transport_error, "publication endpoint must be an HTTPS base URL");
    if (bearerToken.empty() || bearerToken.find_first_of("\r\n") != std::string::npos)
      return submission_result(PublicationTransportStatus::unauthorized, "publication token is missing or malformed");
    try
    {
      json body = {
        {"protocol", "vix-package-publication-v1"},
        {"registry", submission.identity.registry},
        {"publisher", submission.identity.publisher},
        {"package", submission.identity.name},
        {"version", version_text(submission.version)},
        {"contract_toml", submission.contractToml},
        {"content_id", submission.contentId.digest()},
        {"content_p10o_base64", base64(submission.canonicalContent)},
      };
      auto base = endpoint;
      while (!base.empty() && base.back() == '/') base.pop_back();
      vix::requests::RequestOptions options;
      options.headers.set("Authorization", "Bearer " + bearerToken);
      options.headers.set("Accept", "application/json");
      options.follow_redirects = false;
      options.verify_tls = true;
      vix::requests::Client client;
      const auto response = client.post(base + "/packages", vix::requests::json_body(body.dump()), options);
      if (response.status_code() >= 200 && response.status_code() < 300)
      {
        const auto parsed = response_json(response.body());
        if (parsed && parsed->is_object() && parsed->contains("status") && (*parsed)["status"].is_string())
        {
          const auto status = response_status((*parsed)["status"].get<std::string>());
          return submission_result(status);
        }
        return submission_result(PublicationTransportStatus::transport_error,
          "Registry authority returned a malformed publication response");
      }
      if (response.status_code() == 409)
      {
        const auto parsed = response_json(response.body());
        if (parsed && parsed->is_object() && parsed->contains("status") && (*parsed)["status"].is_string() &&
            (*parsed)["status"].get<std::string>() == "already_exists_same_content")
          return submission_result(PublicationTransportStatus::already_exists_same_content);
        return submission_result(PublicationTransportStatus::version_conflict);
      }
      return submission_result(http_status(response.status_code()),
        "Registry authority returned HTTP status " + std::to_string(response.status_code()));
    }
    catch (const std::exception &)
    {
      return submission_result(PublicationTransportStatus::transport_error,
        "publication HTTPS request failed");
    }
  }

  const PublicationTarget *RegistryPublicationDirectory::find(std::string_view registryId) const noexcept
  {
    const auto found = std::find_if(targets.begin(), targets.end(), [&](const auto &target)
      { return target.registryId == registryId; });
    return found == targets.end() ? nullptr : &*found;
  }

  fs::path registry_publication_configuration_path()
  {
    if (const char *overridePath = vix::env::legacy::getenv("VIX_PUBLISH_CONFIG"); overridePath && *overridePath)
      return fs::path{overridePath};
    if (const char *xdg = vix::env::legacy::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
      return fs::path{xdg} / "vix" / "publishing.toml";
    if (const char *home = vix::env::legacy::getenv("HOME"); home && *home)
      return fs::path{home} / ".config" / "vix" / "publishing.toml";
    return {};
  }

  RegistryPublicationConfigurationResult load_registry_publication_directory(const fs::path &path)
  {
    if (path.empty())
      return {{}, PackagePublicationErrorCode::publication_config_unavailable,
        "cannot determine publication config path; set VIX_PUBLISH_CONFIG, XDG_CONFIG_HOME, or HOME"};
    std::error_code ec;
    if (!fs::is_regular_file(path, ec))
      return {{}, PackagePublicationErrorCode::publication_config_unavailable,
        "publication config is not available at " + path.string()};
    try
    {
      const auto document = toml::parse_file(path.string());
      strict_table_keys(document, {"format", "registries"}, "publication config");
      const auto format = document["format"].value<std::int64_t>();
      if (!format || *format != 1) throw std::runtime_error("publishing.toml format must be 1");
      const auto *registriesNode = document.get("registries");
      const auto *registries = registriesNode ? registriesNode->as_table() : nullptr;
      if (!registries) throw std::runtime_error("publishing.toml must contain a [registries] table");
      RegistryPublicationDirectory directory;
      for (const auto &[key, value] : *registries)
      {
        const std::string registryId{key.str()};
        const auto *table = value.as_table();
        if (!table || !valid_registry_id(registryId))
          throw std::runtime_error("each publication registry must be a table with a valid RegistryId");
        strict_table_keys(*table, {"endpoint", "token_env"}, "publication registry");
        const auto endpoint = table->get("endpoint") ? table->get("endpoint")->value<std::string>() : std::nullopt;
        const auto tokenEnv = table->get("token_env") ? table->get("token_env")->value<std::string>() : std::nullopt;
        if (!endpoint || !valid_endpoint(*endpoint))
          throw std::runtime_error("publication endpoint must be an HTTPS base URL");
        if (!tokenEnv || !valid_token_environment_name(*tokenEnv))
          throw std::runtime_error("publication token_env must be a valid environment-variable name");
        directory.targets.push_back({registryId, *endpoint, *tokenEnv});
      }
      return {std::move(directory), PackagePublicationErrorCode::none, {}};
    }
    catch (const std::exception &error)
    {
      return {{}, PackagePublicationErrorCode::publication_config_invalid, error.what()};
    }
  }

  PackagePublicationResult submit_package_publication(const fs::path &packageRoot,
    const PublicationTarget &target, const std::string &bearerToken, PackagePublicationTransport &transport)
  {
    return submit_prepared(prepare_package_publication(packageRoot), target, bearerToken, transport);
  }

  PackagePublicationResult submit_package_publication(const fs::path &packageRoot,
    const RegistryPublicationDirectory &directory, const PublicationTokenLookup &tokenLookup,
    PackagePublicationTransport &transport)
  {
    auto prepared = prepare_package_publication(packageRoot);
    if (!prepared.valid() || !prepared.submission) return prepared;
    const auto *target = directory.find(prepared.submission->identity.registry);
    if (!target)
      return failure(PackagePublicationErrorCode::publication_target_not_configured,
        "no publication target configured for Registry " + prepared.submission->identity.registry,
        std::move(prepared.submission));
    const auto token = tokenLookup ? tokenLookup(target->tokenEnv) : std::nullopt;
    if (!token || token->empty())
      return failure(PackagePublicationErrorCode::credential_missing,
        "credential environment variable " + target->tokenEnv + " is not set", std::move(prepared.submission));
    return submit_prepared(std::move(prepared), *target, *token, transport);
  }

  int run_native_publish_command(const std::vector<std::string> &arguments, const fs::path &packageRoot)
  {
    if (!arguments.empty())
    {
      std::cerr << "vix: usage: vix publish\n";
      return 2;
    }
    const auto configPath = registry_publication_configuration_path();
    const auto configuration = load_registry_publication_directory(configPath);
    if (!configuration.valid())
    {
      std::cerr << "vix: publish error " << static_cast<int>(configuration.code) << ": " << configuration.diagnostic << '\n';
      return 1;
    }
    RequestsPackagePublicationTransport transport;
    const auto result = submit_package_publication(packageRoot, *configuration.directory,
      [](std::string_view name) -> std::optional<std::string>
      {
        const std::string key{name};
        return env_value(key.c_str());
      }, transport);
    if (!result.valid())
    {
      std::cerr << "vix: publish error " << static_cast<int>(result.code) << ": " << result.diagnostic << '\n';
      return 1;
    }
    const auto &submission = *result.submission;
    std::cout << "published " << submission.identity.publisher << '/' << submission.identity.name << '@'
      << version_text(submission.version) << " (" << submission.contentId.digest() << ")\n";
    return 0;
  }
}
