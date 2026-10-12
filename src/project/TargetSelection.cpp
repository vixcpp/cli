/** @file TargetSelection.cpp */
#include <vix/cli/project/TargetSelection.hpp>

#include <algorithm>

namespace vix::cli::project
{
  namespace
  {
    bool matches_filter(const Target &target, TargetFilter filter) noexcept
    {
      switch (filter)
      {
      case TargetFilter::all: return true;
      case TargetFilter::buildable: return target.capabilities.buildable;
      case TargetFilter::runnable: return target.capabilities.runnable;
      }
      return false;
    }

    void sort_targets(std::vector<Target> &targets)
    {
      std::sort(targets.begin(), targets.end(), [](const Target &left, const Target &right)
      {
        return left.name == right.name ? left.id.value < right.id.value : left.name < right.name;
      });
    }

  } // namespace

  std::vector<Target> list_targets(const ProjectSnapshot &snapshot, TargetFilter filter)
  {
    std::vector<Target> targets;
    for (const Target &target : snapshot.project.targets)
      if (matches_filter(target, filter))
        targets.push_back(target);
    sort_targets(targets);
    return targets;
  }

  TargetSelectionResult find_target_by_id(const ProjectSnapshot &snapshot, const TargetId &id)
  {
    for (const Target &target : snapshot.project.targets)
      if (target.id == id)
        return {target, std::nullopt};
    return {std::nullopt, ProjectError::target_not_found};
  }

  TargetSelectionResult select_target_by_name(const ProjectSnapshot &snapshot, std::string_view name)
  {
    const Target *match = nullptr;
    for (const Target &target : snapshot.project.targets)
    {
      if (target.name != name)
        continue;
      if (match != nullptr)
        return {std::nullopt, ProjectError::ambiguous_target};
      match = &target;
    }
    return match == nullptr
        ? TargetSelectionResult{std::nullopt, ProjectError::target_not_found}
        : TargetSelectionResult{*match, std::nullopt};
  }

  TargetSelectionResult select_buildable_target_by_name(
      const ProjectSnapshot &snapshot,
      std::string_view name)
  {
    auto result = select_target_by_name(snapshot, name);
    if (result.success() && !result.target->capabilities.buildable)
      return {std::nullopt, ProjectError::target_not_buildable};
    return result;
  }

  TargetSelectionResult select_runnable_target_by_name(
      const ProjectSnapshot &snapshot,
      std::string_view name)
  {
    auto result = select_target_by_name(snapshot, name);
    if (result.success() && !result.target->capabilities.runnable)
      return {std::nullopt, ProjectError::target_not_runnable};
    return result;
  }

  TargetSelectionResult select_default_runnable_target(const ProjectSnapshot &snapshot)
  {
    const auto runnable = list_targets(snapshot, TargetFilter::runnable);
    if (runnable.empty())
      return {std::nullopt, ProjectError::target_not_runnable};
    if (runnable.size() != 1)
      return {std::nullopt, ProjectError::ambiguous_target};
    return {runnable.front(), std::nullopt};
  }

  std::vector<TargetArtifact> artifacts_for_target(const ProjectSnapshot &snapshot, const TargetId &id)
  {
    std::vector<TargetArtifact> artifacts;
    for (const TargetArtifact &artifact : snapshot.artifacts)
      if (artifact.target == id)
        artifacts.push_back(artifact);
    return artifacts;
  }
} // namespace vix::cli::project
