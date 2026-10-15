/**
 * @file ProjectDiscovery.cpp
 * @brief Canonical Vix project-location discovery implementation.
 */
#include <vix/cli/project/ProjectDiscovery.hpp>

#include <system_error>

namespace vix::cli::project
{
  namespace
  {
    [[nodiscard]] bool marker_is_regular_file(const Path &path)
    {
      std::error_code error;
      return std::filesystem::is_regular_file(path, error) && !error;
    }

    [[nodiscard]] Path normalized_start(const Path &input)
    {
      std::error_code error;
      Path absolute = std::filesystem::absolute(input, error);
      if (error)
        absolute = input;

      absolute = absolute.lexically_normal();
      return marker_is_regular_file(absolute) ? absolute.parent_path() : absolute;
    }
  } // namespace

  ProjectDiscoveryResult discover_project(const Path &input)
  {
    Path current = normalized_start(input);

    while (!current.empty())
    {
      const bool hasCMake = marker_is_regular_file(current / "CMakeLists.txt");
      const bool hasNative = marker_is_regular_file(current / "vix.toml");
      if (hasCMake && hasNative)
        return {std::nullopt, ProjectError::ambiguous_project_authority};
      if (hasNative)
        return {ProjectLocation{current, ProjectSystem::native_vix}, std::nullopt};
      if (hasCMake)
        return {ProjectLocation{current, ProjectSystem::cmake}, std::nullopt};

      const Path parent = current.parent_path();
      if (parent == current)
        break;
      current = parent;
    }

    return {std::nullopt, ProjectError::project_not_found};
  }
} // namespace vix::cli::project
