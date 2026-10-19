#include <vix/cli/project/native/ResolutionCatalog.hpp>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

#include <vix/cli/project/native/Validation.hpp>

namespace vix::cli::project::native
{
  namespace
  {
    bool bytewise_less(std::string_view left, std::string_view right)
    {
      const auto size = std::min(left.size(), right.size());
      for (std::size_t i = 0; i < size; ++i)
      {
        const auto a = static_cast<unsigned char>(left[i]);
        const auto b = static_cast<unsigned char>(right[i]);
        if (a != b) return a < b;
      }
      return left.size() < right.size();
    }

    bool same_revision(const GitRevisionRequirement &left, const GitRevisionRequirement &right)
    {
      if (left.index() != right.index()) return false;
      return std::visit([&](const auto &value)
      {
        using Revision = std::decay_t<decltype(value)>;
        const auto *other = std::get_if<Revision>(&right);
        return other && value.value == other->value;
      }, left);
    }

    std::string revision_key(const GitRevisionRequirement &revision)
    {
      return std::visit([](const auto &value)
      {
        using Revision = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Revision, CommitRevision>)
          return std::string{"commit:"} + value.value;
        else if constexpr (std::is_same_v<Revision, TagRevision>)
          return std::string{"tag:"} + value.value;
        else
          return std::string{"branch:"} + value.value;
      }, revision);
    }

    bool valid_digest(std::string_view digest)
    {
      const auto separator = digest.find(':');
      if (separator == std::string_view::npos) return false;
      const auto algorithm = digest.substr(0, separator);
      const auto hex = digest.substr(separator + 1);
      const bool validLength = (algorithm == "sha256" && hex.size() == 64) ||
        (algorithm == "sha512" && hex.size() == 128);
      return validLength && std::all_of(hex.begin(), hex.end(), [](char c)
      { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }

    bool valid_repository_identity(std::string_view repository)
    {
      if (repository.empty() || std::any_of(repository.begin(), repository.end(), [](unsigned char c)
          { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; })) return false;
      const auto scheme = repository.find("://");
      if (scheme != std::string_view::npos)
      {
        const auto authorityStart = scheme + 3;
        const auto authorityEnd = repository.find_first_of("/?#", authorityStart);
        const auto authority = repository.substr(authorityStart,
          authorityEnd == std::string_view::npos ? repository.size() - authorityStart : authorityEnd - authorityStart);
        if (authority.empty() || authority.find('@') != std::string_view::npos) return false;
      }
      return true;
    }

    bool same_identity(const PackageContractIdentity &left, const PackageContractIdentity &right)
    {
      if (left.index() != right.index()) return false;
      if (const auto *registry = std::get_if<RegistryPackageContractIdentity>(&left))
      {
        const auto &other = std::get<RegistryPackageContractIdentity>(right);
        return registry->identity == other.identity && registry->version == other.version;
      }
      return true;
    }

    bool same_version_requirement(const VersionRequirement &left, const VersionRequirement &right)
    {
      if (left.index() != right.index()) return false;
      return std::visit([&](const auto &mode)
      {
        using Mode = std::decay_t<decltype(mode)>;
        const auto *other = std::get_if<Mode>(&right);
        return other && mode.version == other->version;
      }, left);
    }

    bool same_requirement(const PackageRequirement &left, const PackageRequirement &right)
    {
      if (left.alias != right.alias || left.source.index() != right.source.index()) return false;
      if (const auto *registry = std::get_if<RegistryRequirement>(&left.source))
      {
        const auto &other = std::get<RegistryRequirement>(right.source);
        return registry->identity == other.identity && same_version_requirement(registry->version, other.version);
      }
      const auto &git = std::get<GitRequirement>(left.source);
      const auto &other = std::get<GitRequirement>(right.source);
      return git.normalizedRepository == other.normalizedRepository && git.repositoryRoot == other.repositoryRoot &&
        same_revision(git.revision, other.revision);
    }

    template <typename Left, typename Right, typename Equal>
    bool same_unordered(const Left &left, const Right &right, Equal equal)
    {
      if (left.size() != right.size()) return false;
      std::vector<bool> matched(right.size(), false);
      for (const auto &item : left)
      {
        bool found = false;
        for (std::size_t i = 0; i < right.size(); ++i)
          if (!matched[i] && equal(item, right[i]))
          {
            matched[i] = true;
            found = true;
            break;
          }
        if (!found) return false;
      }
      return true;
    }

    bool same_export(const PackageExport &left, const PackageExport &right)
    {
      if (left.name != right.name || left.mapping.index() != right.mapping.index()) return false;
      if (const auto *native = std::get_if<NativeExportMapping>(&left.mapping))
        return native->projectTarget == std::get<NativeExportMapping>(right.mapping).projectTarget;
      return std::get<CMakeExportMapping>(left.mapping).cmakeTarget ==
        std::get<CMakeExportMapping>(right.mapping).cmakeTarget;
    }

    bool same_contract(const PackageContract &left, const PackageContract &right)
    {
      if (left.format != right.format || !same_identity(left.identity, right.identity) ||
          left.integration.index() != right.integration.index()) return false;
      if (const auto *native = std::get_if<NativeVixIntegration>(&left.integration))
      {
        const auto &other = std::get<NativeVixIntegration>(right.integration);
        if (!same_unordered(native->includeRoots, other.includeRoots, [](const auto &a, const auto &b) { return a == b; })) return false;
      }
      else if (std::get<CMakeConfigIntegration>(left.integration).configName !=
               std::get<CMakeConfigIntegration>(right.integration).configName) return false;
      return same_unordered(left.packages, right.packages, same_requirement) &&
        same_unordered(left.exports, right.exports, same_export);
    }

    ResolutionCatalogIssue catalog_issue(ResolutionCatalogErrorCode code, std::string subject, std::string message)
    {
      return {code, std::move(subject), std::move(message)};
    }

    std::string registry_key(const RegistryCandidate &candidate)
    {
      return candidate.identity.registry + "/" + candidate.identity.publisher + "/" + candidate.identity.name + "@" +
        std::to_string(candidate.version.major) + "." + std::to_string(candidate.version.minor) + "." +
        std::to_string(candidate.version.patch) + (candidate.version.prerelease ? "-" + *candidate.version.prerelease : "");
    }

    bool registry_key_equal(const RegistryCandidate &left, const RegistryCandidate &right)
    {
      return left.identity == right.identity && left.version == right.version;
    }

    bool git_declaration_equal(const GitResolution &left, const GitResolution &right)
    {
      return left.normalizedRepository == right.normalizedRepository && same_revision(left.requestedRevision, right.requestedRevision);
    }
  }

  ResolutionCatalog::ResolutionCatalog(std::vector<RegistryCandidate> registry, std::vector<GitResolution> git)
    : registry_(std::move(registry)), git_(std::move(git))
  {
  }

  std::vector<RegistryCandidate> ResolutionCatalog::registry_candidates(const RegistryPackageIdentity &identity) const
  {
    std::vector<RegistryCandidate> result;
    for (const auto &candidate : registry_)
      if (candidate.identity == identity) result.push_back(candidate);
    return result;
  }

  std::vector<GitResolution> ResolutionCatalog::git_resolutions(
    const std::string &normalizedRepository, const GitRevisionRequirement &revision) const
  {
    std::vector<GitResolution> result;
    for (const auto &resolution : git_)
      if (resolution.normalizedRepository == normalizedRepository && same_revision(resolution.requestedRevision, revision))
        result.push_back(resolution);
    return result;
  }

  ResolutionCatalogBuildResult build_resolution_catalog(
    std::vector<RegistryCandidate> registry, std::vector<GitResolution> git)
  {
    ResolutionCatalogBuildResult result;
    for (const auto &candidate : registry)
    {
      const auto key = registry_key(candidate);
      if (!valid_digest(candidate.contentDigest))
        result.issues.push_back(catalog_issue(ResolutionCatalogErrorCode::invalid_content_digest, key,
          "Registry release content digest has invalid canonical syntax"));
      const auto semantic = validate_package_contract(candidate.contract);
      if (!semantic.valid())
        result.issues.push_back(catalog_issue(ResolutionCatalogErrorCode::invalid_contract, key,
          "Registry release contains an invalid package contract"));
      const auto *identity = std::get_if<RegistryPackageContractIdentity>(&candidate.contract.identity);
      if (!identity || identity->identity != candidate.identity)
        result.issues.push_back(catalog_issue(ResolutionCatalogErrorCode::registry_contract_identity_mismatch, key,
          "Registry package contract identity does not match release metadata"));
      else if (identity->version != candidate.version)
        result.issues.push_back(catalog_issue(ResolutionCatalogErrorCode::registry_contract_version_mismatch, key,
          "Registry package contract version does not match release metadata"));
    }
    for (std::size_t i = 0; i < registry.size(); ++i)
      for (std::size_t j = i + 1; j < registry.size(); ++j)
        if (registry_key_equal(registry[i], registry[j]))
        {
          const auto code = registry[i].contentDigest == registry[j].contentDigest &&
              same_contract(registry[i].contract, registry[j].contract)
            ? ResolutionCatalogErrorCode::duplicate_registry_release
            : ResolutionCatalogErrorCode::conflicting_registry_release;
          result.issues.push_back(catalog_issue(code, registry_key(registry[i]),
            code == ResolutionCatalogErrorCode::duplicate_registry_release
              ? "duplicate Registry release metadata" : "contradictory metadata for one immutable Registry release"));
        }

    for (const auto &resolution : git)
    {
      const auto key = resolution.normalizedRepository + " (" + revision_key(resolution.requestedRevision) + ")";
      if (!valid_repository_identity(resolution.normalizedRepository))
        result.issues.push_back(catalog_issue(ResolutionCatalogErrorCode::invalid_repository_identity, key,
          "Git repository identity must be nonempty, whitespace-free, and credential-free"));
      if ((resolution.commit.size() != 40 && resolution.commit.size() != 64) ||
          !std::all_of(resolution.commit.begin(), resolution.commit.end(), [](char c)
            { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        result.issues.push_back(catalog_issue(ResolutionCatalogErrorCode::invalid_repository_identity, key,
          "Git resolution must contain a full lowercase hexadecimal commit"));
      if (!valid_digest(resolution.contentDigest))
        result.issues.push_back(catalog_issue(ResolutionCatalogErrorCode::invalid_content_digest, key,
          "Git content digest has invalid canonical syntax"));
      if (!validate_package_contract(resolution.contract).valid())
        result.issues.push_back(catalog_issue(ResolutionCatalogErrorCode::invalid_contract, key,
          "Git resolution contains an invalid package contract"));
      if (!std::holds_alternative<GitPackageContractIdentity>(resolution.contract.identity))
        result.issues.push_back(catalog_issue(ResolutionCatalogErrorCode::invalid_git_contract_source, key,
          "Git resolution requires a Git package contract"));
      if (std::visit([](const auto &revision) { return revision.value.empty(); }, resolution.requestedRevision))
        result.issues.push_back(catalog_issue(ResolutionCatalogErrorCode::invalid_repository_identity, key,
          "Git declared revision must be nonempty"));
    }

    for (std::size_t i = 0; i < git.size(); ++i)
      for (std::size_t j = i + 1; j < git.size(); ++j)
      {
        if (git_declaration_equal(git[i], git[j]))
        {
          const auto code = git[i].commit == git[j].commit && git[i].contentDigest == git[j].contentDigest &&
              same_contract(git[i].contract, git[j].contract)
            ? ResolutionCatalogErrorCode::duplicate_git_revision
            : ResolutionCatalogErrorCode::conflicting_git_revision;
          result.issues.push_back(catalog_issue(code, git[i].normalizedRepository,
            code == ResolutionCatalogErrorCode::duplicate_git_revision
              ? "duplicate Git revision resolution metadata" : "one Git revision maps to contradictory immutable metadata"));
        }
        if (!git_declaration_equal(git[i], git[j]) &&
            git[i].normalizedRepository == git[j].normalizedRepository && git[i].commit == git[j].commit &&
            (git[i].contentDigest != git[j].contentDigest || !same_contract(git[i].contract, git[j].contract)))
          result.issues.push_back(catalog_issue(ResolutionCatalogErrorCode::conflicting_git_commit_metadata,
            git[i].normalizedRepository + "@" + git[i].commit,
            "the same Git commit has contradictory content digest or package contract metadata"));
      }

    if (!result.issues.empty()) return result;
    std::sort(registry.begin(), registry.end(), [](const auto &left, const auto &right)
    {
      if (left.identity.registry != right.identity.registry) return bytewise_less(left.identity.registry, right.identity.registry);
      if (left.identity.publisher != right.identity.publisher) return bytewise_less(left.identity.publisher, right.identity.publisher);
      if (left.identity.name != right.identity.name) return bytewise_less(left.identity.name, right.identity.name);
      if (left.version.major != right.version.major) return left.version.major < right.version.major;
      if (left.version.minor != right.version.minor) return left.version.minor < right.version.minor;
      if (left.version.patch != right.version.patch) return left.version.patch < right.version.patch;
      return left.version.prerelease < right.version.prerelease;
    });
    std::sort(git.begin(), git.end(), [](const auto &left, const auto &right)
    {
      if (left.normalizedRepository != right.normalizedRepository)
        return bytewise_less(left.normalizedRepository, right.normalizedRepository);
      if (revision_key(left.requestedRevision) != revision_key(right.requestedRevision))
        return bytewise_less(revision_key(left.requestedRevision), revision_key(right.requestedRevision));
      return bytewise_less(left.commit, right.commit);
    });
    result.value = ResolutionCatalog{std::move(registry), std::move(git)};
    return result;
  }
} // namespace vix::cli::project::native
