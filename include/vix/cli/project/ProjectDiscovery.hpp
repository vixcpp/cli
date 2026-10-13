/**
 * @file ProjectDiscovery.hpp
 * @brief Canonical Vix project-location discovery.
 */
#ifndef VIX_CLI_PROJECT_PROJECT_DISCOVERY_HPP
#define VIX_CLI_PROJECT_PROJECT_DISCOVERY_HPP

#include <optional>

#include <vix/cli/project/ProjectError.hpp>
#include <vix/cli/project/ProjectLocation.hpp>

namespace vix::cli::project
{
  struct ProjectDiscoveryResult
  {
    std::optional<ProjectLocation> location;
    std::optional<ProjectError> error;

    [[nodiscard]] bool success() const noexcept { return location.has_value(); }
  };

  /**
   * Finds the nearest currently recognized project containing input.
   */
  [[nodiscard]] ProjectDiscoveryResult discover_project(const Path &input);
} // namespace vix::cli::project

#endif // VIX_CLI_PROJECT_PROJECT_DISCOVERY_HPP
