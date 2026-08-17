#ifndef VIX_CLI_MODULES_DEPENDENCY_OWNERSHIP_HPP
#define VIX_CLI_MODULES_DEPENDENCY_OWNERSHIP_HPP

#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/app/AppManifest.hpp>
#include <vix/cli/modules/ModuleGraph.hpp>

namespace vix::cli::modules
{
  enum class DependencyOwnerKind { Application, Module };
  enum class DependencySource { Registry, Git };
  enum class DependencyVisibility { Private, Public };

  struct DependencyOwner
  {
    DependencyOwnerKind kind{DependencyOwnerKind::Application};
    std::string module;
    bool active{true};
  };

  // Requirements and CMake links deliberately remain separate: current
  // vix.module arrays do not promise an index-based relationship.
  struct OwnedDependencyRequirement
  {
    DependencyOwner owner;
    DependencySource source{DependencySource::Registry};
    std::string requirement;
  };
  struct OwnedDependencyLink
  {
    DependencyOwner owner;
    DependencySource source{DependencySource::Registry};
    DependencyVisibility visibility{DependencyVisibility::Private};
    std::string target;
  };
  struct DependencyOwnership
  {
    std::vector<OwnedDependencyRequirement> requirements;
    std::vector<OwnedDependencyLink> links;
    std::string error;
    bool success() const { return error.empty(); }
  };

  DependencyOwnership build_dependency_ownership(
      const vix::cli::app::AppManifest &app,
      const ModuleGraph &graph,
      const std::filesystem::path &projectRoot);
}
#endif
