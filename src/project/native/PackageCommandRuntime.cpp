#include <vix/cli/project/native/PackageCommandRuntime.hpp>

#include <vix/cli/project/native/GitPackageMetadataSource.hpp>
#include <vix/cli/project/native/MetadataStorage.hpp>
#include <vix/cli/project/native/NativeDependencyMutation.hpp>
#include <vix/cli/project/native/OnlinePackageMetadataSource.hpp>
#include <vix/cli/project/native/RegistryContentTransport.hpp>
#include <vix/cli/project/native/TomlParser.hpp>
#include <vix/cli/project/native/Validation.hpp>

#include <toml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <vix/requests/Url.hpp>

namespace vix::cli::project::native
{
  namespace fs = std::filesystem;
  namespace
  {
    PackageCommandParseResult<ParsedAddCommand> add_failure(PackageCommandParseCode code, std::string message)
    { return {std::nullopt, {{code, std::move(message)}}}; }
    PackageCommandParseResult<ParsedRemoveCommand> remove_failure(PackageCommandParseCode code, std::string message)
    { return {std::nullopt, {{code, std::move(message)}}}; }
    PackageCommandParseResult<bool> update_failure(PackageCommandParseCode code, std::string message)
    { return {std::nullopt, {{code, std::move(message)}}}; }

    bool valid_git_argument(std::string_view value)
    {
      if (value.empty() || value.find_first_of(" \t\r\n") != std::string_view::npos ||
          value.find('\0') != std::string_view::npos)
        return false;
      const auto scheme = value.find("://");
      const bool url = scheme != std::string_view::npos && scheme > 0 &&
        value.find('/', scheme + 3) != std::string_view::npos;
      const bool scp = value.find('@') != std::string_view::npos && value.find(':') != std::string_view::npos;
      if (!url && !scp)
        return false;
      if (scheme != std::string_view::npos && (value.substr(0, scheme) == "http" ||
          value.substr(0, scheme) == "https") && value.substr(scheme + 3).find('@') != std::string_view::npos)
        return false;
      return true;
    }

    std::optional<std::string> git_alias(std::string_view repository)
    {
      auto end = repository.find_first_of("?#");
      if (end == std::string_view::npos) end = repository.size();
      auto path = repository.substr(0, end);
      while (!path.empty() && path.back() == '/') path.remove_suffix(1);
      const auto slash = path.find_last_of("/:");
      auto name = path.substr(slash == std::string_view::npos ? 0 : slash + 1);
      if (name.ends_with(".git")) name.remove_suffix(4);
      std::string result{"git-"};
      bool hyphen = false;
      for (const char raw : name)
      {
        const auto value = static_cast<unsigned char>(raw);
        if ((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
            (value >= '0' && value <= '9'))
        {
          result.push_back(static_cast<char>(std::tolower(value)));
          hyphen = false;
        }
        else if (!hyphen && !result.empty())
        {
          result.push_back('-');
          hyphen = true;
        }
      }
      while (!result.empty() && result.back() == '-') result.pop_back();
      if (result == "git" || !valid_package_alias(result)) return std::nullopt;
      return result;
    }

    std::optional<std::string> env_value(const char *name)
    {
      const char *value = std::getenv(name);
      if (!value || value[0] == '\0') return std::nullopt;
      return std::string{value};
    }

    bool metadata_location(std::string_view descriptor)
    {
      if (descriptor.starts_with("file:///"))
      {
        const auto path = descriptor.substr(7);
        return !path.empty() && path.front() == '/' && !path.starts_with("//") &&
          descriptor.find_first_of("?#") == std::string_view::npos &&
          descriptor.find('\0') == std::string_view::npos;
      }
      if (!descriptor.starts_with("https://") || descriptor.find_first_of("?#") != std::string_view::npos ||
          descriptor.find('@') != std::string_view::npos)
        return false;
      try
      {
        const auto url = vix::requests::Url::parse(std::string{descriptor});
        return url.is_https() && url.query().empty() && url.fragment().empty();
      }
      catch (...) { return false; }
    }

    std::string node_string(const toml::node &node, std::string_view field)
    {
      const auto value = node.value<std::string>();
      if (!value || value->empty()) throw std::runtime_error(std::string{field} + " must be a non-empty string");
      return *value;
    }

    const toml::node &required_node(const toml::table &table, std::string_view key)
    {
      const auto *node = table.get(key);
      if (!node) throw std::runtime_error("missing field: " + std::string{key});
      return *node;
    }

    void strict_keys(const toml::table &table, std::initializer_list<std::string_view> allowed,
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

    template <typename T>
    const T &required_table(const toml::table &table, std::string_view key)
    {
      const auto *node = table.get(key);
      const auto *value = node ? node->as<T>() : nullptr;
      if (!value) throw std::runtime_error("missing or invalid field: " + std::string{key});
      return *value;
    }

    int report_parse_issue(const PackageCommandParseIssue &issue)
    {
      std::cerr << "vix: " << issue.message << '\n';
      return 2;
    }

    int report_mutation_result(const NativeDependencyMutationResult &result)
    {
      if (result.valid())
      {
        std::cout << "dependency state updated\n";
        return 0;
      }
      if (!result.issues.empty())
        std::cerr << "vix: dependency mutation error " << static_cast<int>(result.issues.front().code)
          << " [" << result.issues.front().subject << "]: " << result.issues.front().message << '\n';
      else
        std::cerr << "vix: dependency mutation failed\n";
      return 1;
    }

    RegistryRuntimeConfigurationResult load_default_registry_config()
    {
      const auto path = registry_runtime_configuration_path();
      if (path.empty()) return {RegistryRuntimeConfiguration{}, {}};
      return load_registry_runtime_configuration(path);
    }

    std::unique_ptr<OnlinePackageMetadataRuntime> make_runtime(
      const RegistryRuntimeConfiguration &configuration, std::string &diagnostic)
    {
      auto metadataRoot = metadata_state_path();
      if (metadataRoot.empty() && !configuration.registries.empty())
      {
        diagnostic = "cannot determine durable Vix metadata state path; set HOME or XDG_STATE_HOME";
        return nullptr;
      }
      try
      {
        const auto gitRoot = fs::temp_directory_path() / "vix-git-metadata-work";
        return OnlinePackageMetadataRuntime::create(configuration, metadataRoot, gitRoot, diagnostic);
      }
      catch (const std::exception &error)
      {
        diagnostic = std::string{"cannot determine temporary Git metadata work path: "} + error.what();
        return nullptr;
      }
    }

    bool requirement_registries_configured(const NativeProject &project,
      const OnlinePackageMetadataRuntime &runtime, std::string &missing)
    {
      for (const auto &requirement : project.packages)
      {
        if (const auto *registry = std::get_if<RegistryRequirement>(&requirement.source))
          if (!runtime.has_registry(registry->identity.registry))
          { missing = registry->identity.registry; return false; }
      }
      return true;
    }

    bool requirement_matches_identity(const PackageRequirement &requirement, const PackageIdentity &identity)
    {
      if (const auto *registry = std::get_if<RegistryRequirement>(&requirement.source))
      {
        const auto *requested = std::get_if<RegistryPackageIdentity>(&identity);
        return requested && registry->identity == *requested;
      }
      const auto &git = std::get<GitRequirement>(requirement.source);
      const auto *requested = std::get_if<GitPackageIdentity>(&identity);
      return requested && git.normalizedRepository == requested->normalizedRepository &&
        git.repositoryRoot == requested->repositoryRoot;
    }

    std::optional<PackageRequirement> infer_add_requirement(const ParsedAddCommand &parsed,
      PackageMetadataSource &source, GitDefaultBranchSource *gitDefaults, std::string &diagnostic)
    {
      if (parsed.requirement) return parsed.requirement;
      if (parsed.implicitStableRegistry)
      {
        const auto query = source.registry_releases(*parsed.implicitStableRegistry);
        if (!query.succeeded)
        {
          diagnostic = query.diagnostic.empty() ? "verified Registry release metadata is unavailable" : query.diagnostic;
          return std::nullopt;
        }
        const RegistryCandidate *selected = nullptr;
        for (const auto &candidate : query.releases)
        {
          if (candidate.identity != *parsed.implicitStableRegistry || candidate.version.prerelease) continue;
          if (!selected || std::tie(selected->version.major, selected->version.minor, selected->version.patch) <
              std::tie(candidate.version.major, candidate.version.minor, candidate.version.patch))
            selected = &candidate;
        }
        if (!selected)
        {
          diagnostic = "no stable release is available for " + parsed.implicitStableRegistry->publisher + "/" +
            parsed.implicitStableRegistry->name + "; supply an explicit --version";
          return std::nullopt;
        }
        PackageRequirement requirement{parsed.implicitAlias,
          RegistryRequirement{*parsed.implicitStableRegistry, CompatibleVersion{selected->version}}};
        const auto valid = validate_native_project(NativeProject{1, "cli-check", std::nullopt, {}, {}, {requirement}});
        if (!valid.valid())
        {
          diagnostic = valid.issues.front().message;
          return std::nullopt;
        }
        return requirement;
      }
      if (parsed.implicitDefaultBranch)
      {
        if (!gitDefaults)
        {
          diagnostic = "Git default-branch discovery is unavailable";
          return std::nullopt;
        }
        const auto discovered = gitDefaults->discover_default_branch(*parsed.implicitDefaultBranch);
        if (!discovered.valid())
        {
          diagnostic = discovered.diagnostic.empty() ? "Git default branch could not be determined" : discovered.diagnostic;
          return std::nullopt;
        }
        PackageRequirement requirement{parsed.implicitAlias,
          GitRequirement{parsed.implicitDefaultBranch->normalizedRepository, true,
            BranchRevision{*discovered.branch}}};
        const auto valid = validate_native_project(NativeProject{1, "cli-check", std::nullopt, {}, {}, {requirement}});
        if (!valid.valid())
        {
          diagnostic = valid.issues.front().message;
          return std::nullopt;
        }
        return requirement;
      }
      diagnostic = "add command did not produce a package requirement";
      return std::nullopt;
    }

    class NoMetadataSource final : public PackageMetadataSource
    {
    public:
      RegistryQueryResult registry_releases(const RegistryPackageIdentity &) override
      { return {false, {}, "metadata source must not be called for an empty remaining graph"}; }
      GitQueryResult resolve_git(const GitPackageIdentity &, const GitRevisionRequirement &) override
      { return {GitQueryStatus::failed, std::nullopt, "metadata source must not be called for an empty remaining graph"}; }
    };
  }

  PackageCommandParseResult<ParsedAddCommand> parse_native_add_arguments(
    const std::vector<std::string> &arguments)
  {
    if (arguments.empty()) return add_failure(PackageCommandParseCode::usage,
      "usage: vix add <publisher>/<package> [--version exact=<version>|compatible=<version>] [--registry <id>] | vix add <git-url> [--commit <oid>|--branch <name>|--tag <name>]");
    const auto &package = arguments.front();
    std::optional<std::string> registry;
    std::optional<std::string> constraint;
    std::optional<std::string> commit, branch, tag;
    for (std::size_t index = 1; index < arguments.size(); ++index)
    {
      const auto &flag = arguments[index];
      if (flag != "--registry" && flag != "--version" && flag != "--commit" &&
          flag != "--branch" && flag != "--tag")
        return add_failure(PackageCommandParseCode::usage, "unknown add option: " + flag);
      if (index + 1 == arguments.size() || arguments[index + 1].starts_with("--"))
        return add_failure(PackageCommandParseCode::usage, "missing value for " + flag);
      auto value = arguments[++index];
      auto *slot = flag == "--registry" ? &registry : flag == "--version" ? &constraint :
        flag == "--commit" ? &commit : flag == "--branch" ? &branch : &tag;
      if (*slot) return add_failure(PackageCommandParseCode::usage, "option supplied more than once: " + flag);
      *slot = std::move(value);
    }

    const auto slash = package.find('/');
    const bool registryShape = slash != std::string::npos && slash > 0 && slash + 1 < package.size() &&
      package.find('/', slash + 1) == std::string::npos && package.find("://") == std::string::npos &&
      package.find('@') == std::string::npos && package.find(':') == std::string::npos;
    if (registryShape)
    {
      if (commit || branch || tag)
        return add_failure(PackageCommandParseCode::usage, "Git revision options cannot be used with a Registry package");
      const auto publisher = package.substr(0, slash);
      const auto name = package.substr(slash + 1);
      const auto registryId = registry.value_or("vix");
      if (!valid_registry_id(registryId) || !valid_registry_name(publisher) || !valid_registry_name(name))
        return add_failure(PackageCommandParseCode::invalid_identity, "invalid Registry ID or publisher/package identity");
      const RegistryPackageIdentity identity{registryId, publisher, name};
      if (constraint)
      {
        const auto parsedConstraint = parse_version_requirement_constraint(*constraint);
        if (!parsedConstraint.valid())
          return add_failure(PackageCommandParseCode::invalid_constraint,
            parsedConstraint.issues.empty() ? "invalid version constraint" : parsedConstraint.issues.front().message);
        PackageRequirement requirement{name, RegistryRequirement{identity, *parsedConstraint.value}};
        const auto valid = validate_native_project(NativeProject{1, "cli-check", std::nullopt, {}, {}, {requirement}});
        if (!valid.valid())
          return add_failure(PackageCommandParseCode::invalid_identity, valid.issues.front().message);
        return {ParsedAddCommand{std::move(requirement), std::nullopt, std::nullopt, {}}, {}};
      }
      return {ParsedAddCommand{std::nullopt, identity, std::nullopt, name}, {}};
    }

    if (registry || constraint)
      return add_failure(PackageCommandParseCode::usage, "--registry and --version apply only to Registry packages");
    if (!valid_git_argument(package))
      return add_failure(PackageCommandParseCode::invalid_identity, "expected publisher/package or a supported Git repository URL");
    const auto revisionCount = static_cast<unsigned>(commit.has_value()) + static_cast<unsigned>(branch.has_value()) +
      static_cast<unsigned>(tag.has_value());
    if (revisionCount > 1)
      return add_failure(PackageCommandParseCode::invalid_revision, "Git add accepts at most one of --commit, --branch, or --tag");
    if (commit && ((commit->size() != 40 && commit->size() != 64) ||
        !std::all_of(commit->begin(), commit->end(), [](char value)
          { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); })))
      return add_failure(PackageCommandParseCode::invalid_revision, "--commit requires a full lowercase Git object ID");
    if ((branch && branch->empty()) || (tag && tag->empty()))
      return add_failure(PackageCommandParseCode::invalid_revision, "Git branch/tag name must be non-empty");
    const auto alias = git_alias(package);
    if (!alias || !valid_package_alias(*alias))
      return add_failure(PackageCommandParseCode::invalid_identity, "Git repository URL does not produce a valid package alias");
    const GitPackageIdentity identity{package, true};
    if (revisionCount == 0)
      return {ParsedAddCommand{std::nullopt, std::nullopt, identity, *alias}, {}};
    GitRevisionRequirement revision = commit ? GitRevisionRequirement{CommitRevision{*commit}} :
      branch ? GitRevisionRequirement{BranchRevision{*branch}} : GitRevisionRequirement{TagRevision{*tag}};
    PackageRequirement requirement{*alias, GitRequirement{package, true, std::move(revision)}};
    const auto valid = validate_native_project(NativeProject{1, "cli-check", std::nullopt, {}, {}, {requirement}});
    if (!valid.valid()) return add_failure(PackageCommandParseCode::invalid_identity, valid.issues.front().message);
    return {ParsedAddCommand{std::move(requirement), std::nullopt, std::nullopt, {}}, {}};
  }

  PackageCommandParseResult<ParsedRemoveCommand> parse_native_remove_arguments(
    const std::vector<std::string> &arguments)
  {
    if (arguments.empty()) return remove_failure(PackageCommandParseCode::usage,
      "usage: vix remove <publisher>/<package> [--registry <id>] | vix remove <git-url>");
    std::optional<std::string> registry;
    if (arguments.size() == 3 && arguments[1] == "--registry") registry = arguments[2];
    else if (arguments.size() != 1)
      return remove_failure(PackageCommandParseCode::usage, "remove accepts only an optional --registry for Registry identities");
    const auto &package = arguments.front();
    const auto slash = package.find('/');
    const bool registryShape = slash != std::string::npos && slash > 0 && slash + 1 < package.size() &&
      package.find('/', slash + 1) == std::string::npos && package.find("://") == std::string::npos &&
      package.find('@') == std::string::npos && package.find(':') == std::string::npos;
    if (registryShape)
    {
      const auto id = registry.value_or("vix");
      const auto publisher = package.substr(0, slash), name = package.substr(slash + 1);
      if (!valid_registry_id(id) || !valid_registry_name(publisher) || !valid_registry_name(name))
        return remove_failure(PackageCommandParseCode::invalid_identity, "invalid Registry ID or publisher/package identity");
      return {ParsedRemoveCommand{PackageIdentity{RegistryPackageIdentity{id, publisher, name}}}, {}};
    }
    if (registry) return remove_failure(PackageCommandParseCode::usage, "--registry requires a Registry package identity");
    if (!valid_git_argument(package)) return remove_failure(PackageCommandParseCode::invalid_identity, "invalid Git repository URL");
    return {ParsedRemoveCommand{PackageIdentity{GitPackageIdentity{package, true}}}, {}};
  }

  PackageCommandParseResult<bool> parse_native_update_arguments(const std::vector<std::string> &arguments)
  {
    if (!arguments.empty()) return update_failure(PackageCommandParseCode::usage,
      "usage: vix update (updates all declared root requirements)");
    return {true, {}};
  }

  fs::path registry_runtime_configuration_path()
  {
    if (const auto overridePath = env_value("VIX_REGISTRY_CONFIG")) return fs::path{*overridePath};
    const auto configHome = env_value("XDG_CONFIG_HOME");
    if (configHome) return fs::path{*configHome} / "vix" / "registries.toml";
    const auto home = env_value("HOME");
    return home ? fs::path{*home} / ".config" / "vix" / "registries.toml" : fs::path{};
  }

  fs::path metadata_state_path()
  {
    const auto stateHome = env_value("XDG_STATE_HOME");
    if (stateHome) return fs::path{*stateHome} / "vix" / "metadata";
    const auto home = env_value("HOME");
    return home ? fs::path{*home} / ".local" / "state" / "vix" / "metadata" : fs::path{};
  }

  RegistryRuntimeConfigurationResult load_registry_runtime_configuration(const fs::path &path)
  {
    RegistryRuntimeConfiguration configuration;
    if (path.empty() || !fs::exists(path)) return {std::move(configuration), {}};
    try
    {
      const auto document = toml::parse_file(path.string());
      strict_keys(document, {"format", "registry", "trusted-key"}, "runtime Registry configuration");
      const auto format = document["format"].value<std::int64_t>();
      if (!format || *format != 1) throw std::runtime_error("registries.toml format must be 1");
      if (const auto *registryNode = document.get("registry"))
      {
        const auto *registries = registryNode->as_array();
        if (!registries) throw std::runtime_error("registry must be an array of tables");
        for (const auto &entry : *registries)
        {
          const auto *table = entry.as_table();
          if (!table) throw std::runtime_error("each registry entry must be a table");
          strict_keys(*table, {"id", "authority-id", "locations"}, "registry");
          RegistryDefinition definition;
          definition.id = node_string(required_node(*table, "id"), "registry.id");
          const auto authority = node_string(required_node(*table, "authority-id"), "registry.authority-id");
          const auto &locations = required_table<toml::array>(*table, "locations");
          if (locations.empty()) throw std::runtime_error("registry.locations must contain at least one location");
          for (const auto &location : locations)
          {
            const auto locationValue = location.value<std::string>();
            if (!locationValue || locationValue->empty())
              throw std::runtime_error("registry.locations[] must be a non-empty string");
            auto descriptor = *locationValue;
            if (!metadata_location(descriptor)) throw std::runtime_error("Registry locations must be valid https: or local file: bases");
            definition.locations.push_back({std::move(descriptor)});
          }
          configuration.authorities.push_back({definition.id, authority});
          configuration.registries.push_back(std::move(definition));
        }
      }
      if (const auto *keyNode = document.get("trusted-key"))
      {
        const auto *keys = keyNode->as_array();
        if (!keys) throw std::runtime_error("trusted-key must be an array of tables");
        for (const auto &entry : *keys)
        {
          const auto *table = entry.as_table();
          if (!table) throw std::runtime_error("each trusted-key entry must be a table");
          strict_keys(*table, {"authority-id", "key-id", "algorithm", "public-key"}, "trusted-key");
          const auto authority = node_string(required_node(*table, "authority-id"), "trusted-key.authority-id");
          RegistryMetadataTrustedKey key;
          key.keyId = node_string(required_node(*table, "key-id"), "trusted-key.key-id");
          const auto algorithm = node_string(required_node(*table, "algorithm"), "trusted-key.algorithm");
          key.publicKeyHex = node_string(required_node(*table, "public-key"), "trusted-key.public-key");
          if (algorithm != "ed25519") throw std::runtime_error("trusted-key.algorithm must be ed25519");
          if (key.publicKeyHex.size() != 64 || !std::all_of(key.publicKeyHex.begin(), key.publicKeyHex.end(),
              [](char value) { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); }))
            throw std::runtime_error("trusted-key.public-key must be 64 lowercase hexadecimal characters");
          auto found = std::find_if(configuration.trustedKeys.begin(), configuration.trustedKeys.end(),
            [&](const auto &item) { return item.authorityId == authority; });
          if (found == configuration.trustedKeys.end())
          {
            configuration.trustedKeys.push_back({authority, {{key.keyId, key.publicKeyHex}}});
          }
          else
          {
            if (std::any_of(found->keys.begin(), found->keys.end(), [&](const auto &item) { return item.keyId == key.keyId; }))
              throw std::runtime_error("duplicate trusted authority/key ID pair");
            found->keys.push_back({std::move(key.keyId), std::move(key.publicKeyHex)});
          }
        }
      }
      const auto registryBuild = build_registry_directory(configuration.registries);
      if (!registryBuild.valid()) throw std::runtime_error(registryBuild.issues.front().message);
      const auto authorityBuild = build_registry_authority_directory(configuration.authorities);
      if (!authorityBuild.valid()) throw std::runtime_error(authorityBuild.issues.front().message);
      return {std::move(configuration), {}};
    }
    catch (const std::exception &error)
    {
      return {std::nullopt, error.what()};
    }
  }

  struct OnlinePackageMetadataRuntime::Impl
  {
    RegistryDirectory registries;
    RegistryAuthorityDirectory authorities;
    Ed25519RegistryAuthorityVerifier verifier;
    RequestsRegistryHttpsRequestExecutor httpsExecutor;
    RequestsRegistryMetadataTransport transport;
    TransportRegistryObservationLocationSource observationSource;
    FilesystemMetadataStorage metadataStorage;
    NativeGitCommandRunner gitRunner;
    GitPackageMetadataSource gitSource;
    RegistryOnlinePackageMetadataSource registrySource;
    OnlinePackageMetadataSource onlineSource;

    Impl(RegistryDirectory registryDirectory, RegistryAuthorityDirectory authorityDirectory,
      std::vector<RegistryMetadataTrustedAuthority> keys, const fs::path &metadataRoot,
      const fs::path &gitRoot)
      : registries(std::move(registryDirectory)), authorities(std::move(authorityDirectory)),
        verifier(std::move(keys)), transport(httpsExecutor), observationSource(transport),
        metadataStorage(metadataRoot), gitSource(gitRoot, gitRunner),
        registrySource(registries, authorities, observationSource, verifier, metadataStorage, metadataStorage),
        onlineSource(registrySource, gitSource)
    {}
  };

  OnlinePackageMetadataRuntime::OnlinePackageMetadataRuntime(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
  {}
  OnlinePackageMetadataRuntime::~OnlinePackageMetadataRuntime() = default;
  OnlinePackageMetadataRuntime::OnlinePackageMetadataRuntime(OnlinePackageMetadataRuntime &&) noexcept = default;
  OnlinePackageMetadataRuntime &OnlinePackageMetadataRuntime::operator=(OnlinePackageMetadataRuntime &&) noexcept = default;

  std::unique_ptr<OnlinePackageMetadataRuntime> OnlinePackageMetadataRuntime::create(
    const RegistryRuntimeConfiguration &configuration, const fs::path &metadataRoot,
    const fs::path &gitWorkRoot, std::string &diagnostic)
  {
    const auto registryBuild = build_registry_directory(configuration.registries);
    if (!registryBuild.valid())
    { diagnostic = registryBuild.issues.front().message; return nullptr; }
    const auto authorityBuild = build_registry_authority_directory(configuration.authorities);
    if (!authorityBuild.valid())
    { diagnostic = authorityBuild.issues.front().message; return nullptr; }
    try
    {
      auto impl = std::make_unique<Impl>(std::move(*registryBuild.value), std::move(*authorityBuild.value),
        configuration.trustedKeys, metadataRoot, gitWorkRoot);
      return std::unique_ptr<OnlinePackageMetadataRuntime>{new OnlinePackageMetadataRuntime{std::move(impl)}};
    }
    catch (const std::exception &error)
    { diagnostic = error.what(); return nullptr; }
  }

  PackageMetadataSource &OnlinePackageMetadataRuntime::source() noexcept { return impl_->onlineSource; }
  bool OnlinePackageMetadataRuntime::has_registry(std::string_view registryId) const noexcept
  { return impl_->registries.find(registryId) != nullptr; }
  GitDefaultBranchResult OnlinePackageMetadataRuntime::discover_default_branch(
    const GitPackageIdentity &identity)
  { return impl_->gitSource.discover_default_branch(identity); }

  int run_native_add_command_with_source(const std::vector<std::string> &arguments,
    const fs::path &workingDirectory, PackageMetadataSource &source, GitDefaultBranchSource *gitDefaults)
  {
    const auto parsed = parse_native_add_arguments(arguments);
    if (!parsed.valid()) return report_parse_issue(parsed.issues.front());
    const auto manifest = workingDirectory / "vix.toml";
    if (!fs::is_regular_file(manifest))
    { std::cerr << "vix: native-project-required (vix.toml not found)\n"; return 1; }
    const auto project = parse_native_manifest_file(manifest);
    if (!project.valid())
    { std::cerr << "vix: invalid vix.toml: " << project.issues.front().message << '\n'; return 1; }
    std::string inferenceDiagnostic;
    auto requirement = infer_add_requirement(*parsed.value, source, gitDefaults, inferenceDiagnostic);
    if (!requirement)
    { std::cerr << "vix: " << inferenceDiagnostic << '\n'; return 1; }
    NativeDependencyMutationService service{workingDirectory, source};
    return report_mutation_result(service.add(std::move(*requirement)));
  }

  int run_native_remove_command_with_source(const std::vector<std::string> &arguments,
    const fs::path &workingDirectory, PackageMetadataSource &source)
  {
    const auto parsed = parse_native_remove_arguments(arguments);
    if (!parsed.valid()) return report_parse_issue(parsed.issues.front());
    const auto manifest = workingDirectory / "vix.toml";
    if (!fs::is_regular_file(manifest))
    { std::cerr << "vix: native-project-required (vix.toml not found)\n"; return 1; }
    const auto project = parse_native_manifest_file(manifest);
    if (!project.valid())
    { std::cerr << "vix: invalid vix.toml: " << project.issues.front().message << '\n'; return 1; }
    NativeDependencyMutationService service{workingDirectory, source};
    return report_mutation_result(service.remove_identity(parsed.value->identity));
  }

  int run_native_update_command_with_source(const std::vector<std::string> &arguments,
    const fs::path &workingDirectory, PackageMetadataSource &source)
  {
    const auto parsed = parse_native_update_arguments(arguments);
    if (!parsed.valid()) return report_parse_issue(parsed.issues.front());
    const auto manifest = workingDirectory / "vix.toml";
    if (!fs::is_regular_file(manifest))
    { std::cerr << "vix: native-project-required (vix.toml not found)\n"; return 1; }
    const auto project = parse_native_manifest_file(manifest);
    if (!project.valid())
    { std::cerr << "vix: invalid vix.toml: " << project.issues.front().message << '\n'; return 1; }
    NativeDependencyMutationService service{workingDirectory, source};
    return report_mutation_result(service.update());
  }

  int run_native_add_command(const std::vector<std::string> &arguments, const fs::path &workingDirectory)
  {
    const auto parsed = parse_native_add_arguments(arguments);
    if (!parsed.valid()) return report_parse_issue(parsed.issues.front());
    const auto manifest = workingDirectory / "vix.toml";
    if (!fs::is_regular_file(manifest))
    { std::cerr << "vix: native-project-required (vix.toml not found)\n"; return 1; }
    const auto project = parse_native_manifest_file(manifest);
    if (!project.valid())
    { std::cerr << "vix: invalid vix.toml: " << project.issues.front().message << '\n'; return 1; }
    const auto config = load_default_registry_config();
    if (!config.valid()) { std::cerr << "vix: invalid Registry runtime configuration: " << config.diagnostic << '\n'; return 1; }
    std::string diagnostic;
    auto runtime = make_runtime(*config.configuration, diagnostic);
    if (!runtime) { std::cerr << "vix: cannot initialize online metadata runtime: " << diagnostic << '\n'; return 1; }
    const RegistryPackageIdentity *registryIdentity = parsed.value->implicitStableRegistry ?
      &*parsed.value->implicitStableRegistry : nullptr;
    if (parsed.value->requirement)
      if (const auto *registry = std::get_if<RegistryRequirement>(&parsed.value->requirement->source))
        registryIdentity = &registry->identity;
    if (registryIdentity)
      if (!runtime->has_registry(registryIdentity->registry))
      { std::cerr << "vix: registry_not_configured: " << registryIdentity->registry << '\n'; return 1; }
    return run_native_add_command_with_source(arguments, workingDirectory, runtime->source(), runtime.get());
  }

  int run_native_remove_command(const std::vector<std::string> &arguments, const fs::path &workingDirectory)
  {
    const auto parsed = parse_native_remove_arguments(arguments);
    if (!parsed.valid()) return report_parse_issue(parsed.issues.front());
    const auto manifest = workingDirectory / "vix.toml";
    if (!fs::is_regular_file(manifest))
    { std::cerr << "vix: native-project-required (vix.toml not found)\n"; return 1; }
    const auto currentProject = parse_native_manifest_file(manifest);
    if (!currentProject.valid())
    {
      NoMetadataSource noMetadata;
      NativeDependencyMutationService service{workingDirectory, noMetadata};
      return report_mutation_result(service.remove_identity(parsed.value->identity));
    }
    const auto target = std::find_if(currentProject.value->packages.begin(), currentProject.value->packages.end(),
      [&](const auto &requirement) { return requirement_matches_identity(requirement, parsed.value->identity); });
    if (target == currentProject.value->packages.end())
    {
      NoMetadataSource noMetadata;
      NativeDependencyMutationService service{workingDirectory, noMetadata};
      return report_mutation_result(service.remove_identity(parsed.value->identity));
    }
    if (currentProject.value->packages.size() == 1)
    {
      NoMetadataSource noMetadata;
      NativeDependencyMutationService service{workingDirectory, noMetadata};
      return report_mutation_result(service.remove_identity(parsed.value->identity));
    }
    const auto config = load_default_registry_config();
    if (!config.valid()) { std::cerr << "vix: invalid Registry runtime configuration: " << config.diagnostic << '\n'; return 1; }
    std::string diagnostic;
    auto runtime = make_runtime(*config.configuration, diagnostic);
    if (!runtime) { std::cerr << "vix: cannot initialize online metadata runtime: " << diagnostic << '\n'; return 1; }
    for (auto it = currentProject.value->packages.begin(); it != currentProject.value->packages.end(); ++it)
    {
      if (it == target) continue;
      if (const auto *registry = std::get_if<RegistryRequirement>(&it->source))
        if (!runtime->has_registry(registry->identity.registry))
        { std::cerr << "vix: registry_not_configured: " << registry->identity.registry << '\n'; return 1; }
    }
    return run_native_remove_command_with_source(arguments, workingDirectory, runtime->source());
  }

  int run_native_update_command(const std::vector<std::string> &arguments, const fs::path &workingDirectory)
  {
    const auto parsed = parse_native_update_arguments(arguments);
    if (!parsed.valid()) return report_parse_issue(parsed.issues.front());
    const auto manifest = workingDirectory / "vix.toml";
    if (!fs::is_regular_file(manifest))
    { std::cerr << "vix: native-project-required (vix.toml not found)\n"; return 1; }
    const auto project = parse_native_manifest_file(manifest);
    if (!project.valid())
    { std::cerr << "vix: invalid vix.toml: " << project.issues.front().message << '\n'; return 1; }
    if (project.value->packages.empty())
    {
      NoMetadataSource noMetadata;
      NativeDependencyMutationService service{workingDirectory, noMetadata};
      return report_mutation_result(service.update());
    }
    const auto config = load_default_registry_config();
    if (!config.valid()) { std::cerr << "vix: invalid Registry runtime configuration: " << config.diagnostic << '\n'; return 1; }
    std::string diagnostic;
    auto runtime = make_runtime(*config.configuration, diagnostic);
    if (!runtime) { std::cerr << "vix: cannot initialize online metadata runtime: " << diagnostic << '\n'; return 1; }
    std::string missing;
    if (!requirement_registries_configured(*project.value, *runtime, missing))
    { std::cerr << "vix: registry_not_configured: " << missing << '\n'; return 1; }
    return run_native_update_command_with_source(arguments, workingDirectory, runtime->source());
  }
}
