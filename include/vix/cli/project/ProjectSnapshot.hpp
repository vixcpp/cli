/**
 * @file ProjectSnapshot.hpp
 * @brief Boundary between project identity and configured observations.
 */
#ifndef VIX_CLI_PROJECT_PROJECT_SNAPSHOT_HPP
#define VIX_CLI_PROJECT_PROJECT_SNAPSHOT_HPP

#include <vector>

#include <vix/cli/project/Project.hpp>

namespace vix::cli::project
{
  // Deliberately empty in P2.  Later adapters associate configuration-specific
  // observations with this value without importing native build-system state
  // into Project or Target.
  struct BuildContext {};

  struct TargetArtifact
  {
    TargetId target;
    Path path;
  };

  struct ProjectSnapshot
  {
    Project project;
    BuildContext context;
    std::vector<TargetArtifact> artifacts;
  };
} // namespace vix::cli::project

#endif // VIX_CLI_PROJECT_PROJECT_SNAPSHOT_HPP
