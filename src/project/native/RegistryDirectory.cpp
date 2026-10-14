#include <vix/cli/project/native/RegistryDirectory.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>

#include <vix/cli/project/native/Validation.hpp>

namespace vix::cli::project::native
{
  namespace
  {
    RegistryDirectoryIssue directory_issue(RegistryDirectoryErrorCode code, std::string subject, std::string message)
    {
      return {code, std::move(subject), std::move(message)};
    }

    RegistryRoutingIssue routing_issue(RegistryRoutingErrorCode code, const RegistryPackageIdentity &identity,
                                       std::string location, std::string message)
    {
      return {code, identity.registry, std::move(location), std::move(message)};
    }
  }

  RegistryDirectory::RegistryDirectory(std::vector<RegistryDefinition> definitions)
    : definitions_(std::move(definitions))
  {
  }

  const RegistryDefinition *RegistryDirectory::find(std::string_view registryId) const noexcept
  {
    const auto found = std::lower_bound(definitions_.begin(), definitions_.end(), registryId,
      [](const RegistryDefinition &definition, std::string_view id) { return definition.id < id; });
    return found != definitions_.end() && found->id == registryId ? &*found : nullptr;
  }

  RegistryDirectoryBuildResult build_registry_directory(std::vector<RegistryDefinition> definitions)
  {
    RegistryDirectoryBuildResult result;
    std::sort(definitions.begin(), definitions.end(), [](const auto &left, const auto &right)
    {
      if (left.id != right.id) return left.id < right.id;
      return std::lexicographical_compare(left.locations.begin(), left.locations.end(),
        right.locations.begin(), right.locations.end(), [](const auto &a, const auto &b)
        { return a.descriptor < b.descriptor; });
    });
    for (std::size_t i = 0; i < definitions.size(); ++i)
    {
      auto &definition = definitions[i];
      if (!valid_registry_id(definition.id))
        result.issues.push_back(directory_issue(RegistryDirectoryErrorCode::invalid_registry_id, definition.id,
          "Registry definition ID is not a valid logical Registry identifier"));
      if (definition.locations.empty())
        result.issues.push_back(directory_issue(RegistryDirectoryErrorCode::no_registry_locations, definition.id,
          "Registry definition must contain at least one location"));
      for (std::size_t location = 0; location < definition.locations.size(); ++location)
      {
        if (definition.locations[location].descriptor.empty())
          result.issues.push_back(directory_issue(RegistryDirectoryErrorCode::invalid_location, definition.id,
            "Registry location descriptor must be nonempty"));
        for (std::size_t prior = 0; prior < location; ++prior)
          if (definition.locations[prior] == definition.locations[location])
            result.issues.push_back(directory_issue(RegistryDirectoryErrorCode::duplicate_location, definition.id,
              "Registry definition contains a duplicate location descriptor"));
      }
      if (i > 0 && definitions[i - 1].id == definition.id)
        result.issues.push_back(directory_issue(RegistryDirectoryErrorCode::duplicate_registry_id, definition.id,
          "Registry IDs must have exactly one directory definition"));
    }
    if (!result.issues.empty()) return result;
    result.value = RegistryDirectory{std::move(definitions)};
    return result;
  }

  RegistryRoutingResult route_registry_releases(const RegistryDirectory &directory,
                                                 RegistryMetadataLocationSource &source,
                                                 const RegistryPackageIdentity &identity)
  {
    RegistryRoutingResult result;
    const auto *definition = directory.find(identity.registry);
    if (!definition)
    {
      result.issues.push_back(routing_issue(RegistryRoutingErrorCode::unknown_registry, identity, {},
        "no Registry directory definition exists for this logical Registry ID"));
      return result;
    }

    std::vector<RegistryRoutingIssue> failures;
    for (const auto &location : definition->locations)
    {
      auto observation = source.registry_releases_at(location, identity);
      if (!observation.succeeded)
      {
        failures.push_back(routing_issue(RegistryRoutingErrorCode::location_failure, identity, location.descriptor,
          observation.diagnostic.empty() ? "Registry location failed to provide an observation" : std::move(observation.diagnostic)));
        continue;
      }
      const auto mismatch = std::find_if(observation.releases.begin(), observation.releases.end(), [&](const auto &release)
        { return release.identity != identity; });
      if (mismatch != observation.releases.end())
      {
        result.issues.push_back(routing_issue(RegistryRoutingErrorCode::invalid_location_response, identity, location.descriptor,
          "successful Registry observation contains a release for a different typed Registry identity"));
        return result;
      }
      result.observation = std::move(observation);
      return result;
    }
    result.issues = std::move(failures);
    if (result.issues.empty())
      result.issues.push_back(routing_issue(RegistryRoutingErrorCode::location_failure, identity, {},
        "Registry has no configured metadata locations"));
    return result;
  }

  RoutedPackageMetadataSource::RoutedPackageMetadataSource(
    const RegistryDirectory &directory, RegistryMetadataLocationSource &registrySource, PackageMetadataSource &gitSource)
    : directory_(directory), registrySource_(registrySource), gitSource_(gitSource)
  {
  }

  RegistryQueryResult RoutedPackageMetadataSource::registry_releases(const RegistryPackageIdentity &identity)
  {
    auto routed = route_registry_releases(directory_, registrySource_, identity);
    if (routed.valid()) return std::move(*routed.observation);
    std::string diagnostic;
    for (const auto &issue : routed.issues)
    {
      if (!diagnostic.empty()) diagnostic += "; ";
      diagnostic += issue.code == RegistryRoutingErrorCode::unknown_registry ? "unknown_registry" :
        issue.code == RegistryRoutingErrorCode::invalid_location_response ? "invalid_location_response" : "registry_location_failure";
      diagnostic += " [" + issue.registryId + "]";
      if (!issue.location.empty()) diagnostic += " at " + issue.location;
      diagnostic += ": " + issue.message;
    }
    return {false, {}, std::move(diagnostic)};
  }

  GitQueryResult RoutedPackageMetadataSource::resolve_git(
    const GitPackageIdentity &identity, const GitRevisionRequirement &revision)
  {
    return gitSource_.resolve_git(identity, revision);
  }
} // namespace vix::cli::project::native
