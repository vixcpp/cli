#ifndef VIX_CLI_MODULES_DEPENDENCY_CONSTRAINTS_HPP
#define VIX_CLI_MODULES_DEPENDENCY_CONSTRAINTS_HPP

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <vix/cli/modules/DependencyOwnership.hpp>

namespace vix::cli::modules
{
  struct RegistryRequirement
  {
    std::string packageId;
    std::string requestedVersion;
    std::string originalRequirement;
    DependencyOwner owner;
  };
  struct RegistryConflict
  {
    std::string packageId;
    std::vector<RegistryRequirement> requirements;
    std::string reason;
  };
  struct ResolvedRegistryConstraint
  {
    std::string packageId;
    std::string version;
    std::vector<RegistryRequirement> requirements;
  };
  struct DependencyConstraintResult
  {
    std::vector<RegistryRequirement> declaredRegistryRequirements;
    std::vector<ResolvedRegistryConstraint> resolvedRegistry;
    std::vector<RegistryConflict> conflicts;
    std::string error;
    bool success() const { return error.empty() && conflicts.empty(); }
  };

  // Future in-memory Git constraint model. No module Git syntax is attached.
  struct GitBuildConfiguration { std::string repository, revision, subdirectory; std::vector<std::pair<std::string, std::string>> cmakeOptions; std::vector<std::string> targets; };
  struct GitDependencyConstraint { DependencyOwner owner; GitBuildConfiguration build; };
  struct GitConflict { std::string repository; std::vector<GitDependencyConstraint> constraints; std::string reason; };
  struct GitConstraintResult { std::vector<GitConflict> conflicts; bool success() const { return conflicts.empty(); } };

  DependencyConstraintResult analyze_dependency_constraints(
      const DependencyOwnership &ownership,
      const std::map<std::string, std::vector<std::string>> &availableRegistryVersions);
  std::optional<std::string> registry_dependency_identity(
      const std::string &requirement);
  GitConstraintResult analyze_git_constraints(
      std::vector<GitDependencyConstraint> constraints);
}
#endif
