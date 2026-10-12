/**
 * @file TargetSelection.hpp
 * @brief Deterministic, internal selection over normalized project targets.
 */
#ifndef VIX_CLI_PROJECT_TARGET_SELECTION_HPP
#define VIX_CLI_PROJECT_TARGET_SELECTION_HPP

#include <optional>
#include <string_view>
#include <vector>

#include <vix/cli/project/ProjectError.hpp>
#include <vix/cli/project/ProjectSnapshot.hpp>

namespace vix::cli::project
{
  enum class TargetFilter
  {
    all,
    buildable,
    runnable
  };

  /** A copied target keeps the result independent of the snapshot lifetime. */
  struct TargetSelectionResult
  {
    std::optional<Target> target;
    std::optional<ProjectError> error;

    [[nodiscard]] bool success() const noexcept { return target.has_value(); }
  };

  /** Returns a presentation-ordered copy of targets satisfying the filter. */
  [[nodiscard]] std::vector<Target> list_targets(
      const ProjectSnapshot &snapshot,
      TargetFilter filter = TargetFilter::all);

  /** Resolves an opaque adapter identity, never a display name. */
  [[nodiscard]] TargetSelectionResult find_target_by_id(
      const ProjectSnapshot &snapshot,
      const TargetId &id);

  /** Resolves an exact user-facing target name. */
  [[nodiscard]] TargetSelectionResult select_target_by_name(
      const ProjectSnapshot &snapshot,
      std::string_view name);

  [[nodiscard]] TargetSelectionResult select_buildable_target_by_name(
      const ProjectSnapshot &snapshot,
      std::string_view name);

  [[nodiscard]] TargetSelectionResult select_runnable_target_by_name(
      const ProjectSnapshot &snapshot,
      std::string_view name);

  /** Selects the only runnable target, or reports no/ambiguous candidates. */
  [[nodiscard]] TargetSelectionResult select_default_runnable_target(
      const ProjectSnapshot &snapshot);

  /** Returns every configured artifact associated with an exact target ID. */
  [[nodiscard]] std::vector<TargetArtifact> artifacts_for_target(
      const ProjectSnapshot &snapshot,
      const TargetId &id);
} // namespace vix::cli::project

#endif // VIX_CLI_PROJECT_TARGET_SELECTION_HPP
