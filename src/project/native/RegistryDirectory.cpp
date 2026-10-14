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
      for (std::size_t location = 0; location < definition.contentLocations.size(); ++location)
      {
        if (definition.contentLocations[location].descriptor.empty())
          result.issues.push_back(directory_issue(RegistryDirectoryErrorCode::invalid_content_location, definition.id,
            "Registry content location descriptor must be nonempty"));
        for (std::size_t prior = 0; prior < location; ++prior)
          if (definition.contentLocations[prior] == definition.contentLocations[location])
            result.issues.push_back(directory_issue(RegistryDirectoryErrorCode::duplicate_content_location, definition.id,
              "Registry definition contains a duplicate content location descriptor"));
      }
      if (i > 0 && definitions[i - 1].id == definition.id)
        result.issues.push_back(directory_issue(RegistryDirectoryErrorCode::duplicate_registry_id, definition.id,
          "Registry IDs must have exactly one directory definition"));
    }
    if (!result.issues.empty()) return result;
    result.value = RegistryDirectory{std::move(definitions)};
    return result;
  }

} // namespace vix::cli::project::native
