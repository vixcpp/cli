#include <vix/cli/project/native/MetadataAcquisition.hpp>
#include <vix/cli/project/native/MetadataStorage.hpp>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

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
        if (a != b)
          return a < b;
      }
      return left.size() < right.size();
    }

    void append_field(std::string &key, std::string_view name, std::string_view value)
    {
      key.append(name);
      key.push_back('=');
      key.append(std::to_string(value.size()));
      key.push_back(':');
      key.append(value);
      key.push_back('\n');
    }

    std::string revision_kind(const GitRevisionRequirement &revision)
    {
      return std::visit([](const auto &value)
                        {
        using Revision = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Revision, CommitRevision>) return std::string{"commit"};
        if constexpr (std::is_same_v<Revision, TagRevision>) return std::string{"tag"};
        return std::string{"branch"}; }, revision);
    }

    std::string revision_value(const GitRevisionRequirement &revision)
    {
      return std::visit([](const auto &value)
                        { return value.value; }, revision);
    }

    std::string registry_key(const RegistryPackageIdentity &identity)
    {
      std::string key{"registry\n"};
      append_field(key, "registry", identity.registry);
      append_field(key, "publisher", identity.publisher);
      append_field(key, "name", identity.name);
      return key;
    }

    std::string git_key(const GitPackageIdentity &identity, const GitRevisionRequirement &revision)
    {
      std::string key{"git\n"};
      append_field(key, "repository", identity.normalizedRepository);
      append_field(key, "package-root", identity.repositoryRoot ? "." : "<invalid>");
      append_field(key, "revision-kind", revision_kind(revision));
      append_field(key, "revision", revision_value(revision));
      return key;
    }

    std::string requirement_key(const PackageRequirement &requirement)
    {
      return std::visit([&](const auto &source)
                        {
        using Source = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<Source, RegistryRequirement>) return registry_key(source.identity);
        else return git_key(GitPackageIdentity{source.normalizedRepository, source.repositoryRoot}, source.revision); }, requirement.source);
    }

    std::string identity_text(const RegistryPackageIdentity &identity)
    {
      return "registry:" + identity.registry + "/" + identity.publisher + "/" + identity.name;
    }

    std::string identity_text(const GitPackageIdentity &identity, const GitRevisionRequirement &revision)
    {
      return "git:" + identity.normalizedRepository + "#" + revision_kind(revision) + " " + revision_value(revision);
    }

    MetadataAcquisitionIssue issue(MetadataAcquisitionErrorCode code, std::string subject, std::string message)
    {
      return {code, std::move(subject), std::move(message), std::nullopt};
    }
  }

  MetadataAcquisitionResult acquire_resolution_catalog(
      const std::vector<PackageRequirement> &rootRequirements, PackageMetadataSource &source)
  {
    MetadataAcquisitionResult result;
    std::vector<RegistryCandidate> registryRecords;
    std::vector<GitResolution> gitRecords;
    std::vector<PackageRequirement> frontier = rootRequirements;
    std::vector<std::string> observedRegistry;
    std::vector<std::string> observedGit;

    while (!frontier.empty())
    {
      std::sort(frontier.begin(), frontier.end(), [](const auto &left, const auto &right)
                {
        const auto leftKey = requirement_key(left);
        const auto rightKey = requirement_key(right);
        if (leftKey != rightKey) return bytewise_less(leftKey, rightKey);
        return bytewise_less(left.alias, right.alias); });

      std::vector<PackageRequirement> next;
      for (const auto &requirement : frontier)
      {
        if (const auto *registry = std::get_if<RegistryRequirement>(&requirement.source))
        {
          const auto key = registry_key(registry->identity);
          if (std::find(observedRegistry.begin(), observedRegistry.end(), key) != observedRegistry.end())
            continue;
          observedRegistry.push_back(key);
          auto response = source.registry_releases(registry->identity);
          if (!response.succeeded)
          {
            result.issues.push_back(issue(MetadataAcquisitionErrorCode::registry_source_failure,
                                          identity_text(registry->identity), response.diagnostic.empty() ? "Registry metadata source failed" : std::move(response.diagnostic)));
            return result;
          }
          for (auto &candidate : response.releases)
          {
            if (candidate.identity != registry->identity)
            {
              result.issues.push_back(issue(MetadataAcquisitionErrorCode::invalid_source_response,
                                            identity_text(registry->identity), "Registry response contains a release for a different typed identity"));
              return result;
            }
            next.insert(next.end(), candidate.contract.packages.begin(), candidate.contract.packages.end());
            registryRecords.push_back(std::move(candidate));
          }
        }
        else
        {
          const auto &git = std::get<GitRequirement>(requirement.source);
          const GitPackageIdentity identity{git.normalizedRepository, git.repositoryRoot};
          const auto key = git_key(identity, git.revision);
          if (std::find(observedGit.begin(), observedGit.end(), key) != observedGit.end())
            continue;
          observedGit.push_back(key);
          auto response = source.resolve_git(identity, git.revision);
          const auto subject = identity_text(identity, git.revision);
          if (response.status == GitQueryStatus::failed)
          {
            result.issues.push_back(issue(MetadataAcquisitionErrorCode::git_source_failure, subject,
                                          response.diagnostic.empty() ? "Git metadata source failed" : std::move(response.diagnostic)));
            return result;
          }
          if (response.status == GitQueryStatus::not_found)
          {
            if (response.resolution)
            {
              result.issues.push_back(issue(MetadataAcquisitionErrorCode::invalid_source_response, subject,
                                            "Git source marked a response not-found but also supplied a resolution"));
              return result;
            }
            continue;
          }
          if (!response.resolution)
          {
            result.issues.push_back(issue(MetadataAcquisitionErrorCode::invalid_source_response, subject,
                                          "Git source marked a resolution found but supplied no immutable record"));
            return result;
          }
          auto resolution = std::move(*response.resolution);
          if (resolution.normalizedRepository != identity.normalizedRepository ||
              resolution.requestedRevision.index() != git.revision.index() ||
              revision_value(resolution.requestedRevision) != revision_value(git.revision))
          {
            result.issues.push_back(issue(MetadataAcquisitionErrorCode::invalid_source_response, subject,
                                          "Git response does not match the requested repository and declared revision"));
            return result;
          }
          next.insert(next.end(), resolution.contract.packages.begin(), resolution.contract.packages.end());
          gitRecords.push_back(std::move(resolution));
        }
      }
      frontier = std::move(next);
    }

    auto built = build_resolution_catalog(std::move(registryRecords), std::move(gitRecords));
    if (!built.valid())
    {
      for (auto &catalogIssue : built.issues)
        result.issues.push_back({MetadataAcquisitionErrorCode::catalog_construction_failure,
                                 std::move(catalogIssue.subject), std::move(catalogIssue.message), catalogIssue.code});
      return result;
    }
    result.catalog = std::move(built.value);
    return result;
  }

  MetadataAcquisitionResult acquire_resolution_catalog(
    const std::vector<PackageRequirement> &rootRequirements,
    AcquisitionMode mode,
    PackageMetadataSource &onlineSource,
    OfflineCachedPackageMetadataSource &offlineSource)
  {
    PackageMetadataSource &selectedSource = mode == AcquisitionMode::offline
      ? static_cast<PackageMetadataSource &>(offlineSource)
      : onlineSource;
    return acquire_resolution_catalog(rootRequirements, selectedSource);
  }
} // namespace vix::cli::project::native
