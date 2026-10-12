/**
 * @file ProjectSnapshot.hpp
 * @brief Boundary between project identity and configured observations.
 */
#ifndef VIX_CLI_PROJECT_PROJECT_SNAPSHOT_HPP
#define VIX_CLI_PROJECT_PROJECT_SNAPSHOT_HPP

#include <string>
#include <vector>

#include <vix/cli/project/Project.hpp>

namespace vix::cli::project
{
  // Configuration-specific observation context.  Native generator, compiler,
  // and cache details remain adapter-private.
  struct BuildContext
  {
    Path buildRoot;
    std::string configuration;
  };

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
