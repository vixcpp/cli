/**
 * @file ProjectError.hpp
 * @brief Internal project-model error identities.
 */
#ifndef VIX_CLI_PROJECT_PROJECT_ERROR_HPP
#define VIX_CLI_PROJECT_PROJECT_ERROR_HPP

#include <string_view>

namespace vix::cli::project
{
  enum class ProjectError
  {
    project_not_found,
    ambiguous_project,
    unsupported_project_system,
    configured_metadata_unavailable,
    target_not_found,
    ambiguous_target,
    target_not_buildable,
    target_not_runnable,
    test_not_found,
    ambiguous_test,
    configuration_required,
    configuration_not_found
  };

  [[nodiscard]] constexpr std::string_view to_string(ProjectError error) noexcept
  {
    switch (error)
    {
    case ProjectError::project_not_found: return "project_not_found";
    case ProjectError::ambiguous_project: return "ambiguous_project";
    case ProjectError::unsupported_project_system: return "unsupported_project_system";
    case ProjectError::configured_metadata_unavailable: return "configured_metadata_unavailable";
    case ProjectError::target_not_found: return "target_not_found";
    case ProjectError::ambiguous_target: return "ambiguous_target";
    case ProjectError::target_not_buildable: return "target_not_buildable";
    case ProjectError::target_not_runnable: return "target_not_runnable";
    case ProjectError::test_not_found: return "test_not_found";
    case ProjectError::ambiguous_test: return "ambiguous_test";
    case ProjectError::configuration_required: return "configuration_required";
    case ProjectError::configuration_not_found: return "configuration_not_found";
    }
    return "unknown_project_error";
  }
} // namespace vix::cli::project

#endif // VIX_CLI_PROJECT_PROJECT_ERROR_HPP
