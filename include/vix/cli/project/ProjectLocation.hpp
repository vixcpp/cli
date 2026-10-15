/**
 * @file ProjectLocation.hpp
 * @brief Project discovery result contract.
 */
#ifndef VIX_CLI_PROJECT_PROJECT_LOCATION_HPP
#define VIX_CLI_PROJECT_PROJECT_LOCATION_HPP

#include <vix/cli/project/Project.hpp>

namespace vix::cli::project
{
  enum class ProjectSystem
  {
    cmake,
    native_vix
  };

  struct ProjectLocation
  {
    Path root;
    ProjectSystem system{ProjectSystem::cmake};
  };
} // namespace vix::cli::project

#endif // VIX_CLI_PROJECT_PROJECT_LOCATION_HPP
