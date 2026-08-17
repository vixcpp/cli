#include <vix/cli/modules/DependencyOwnership.hpp>
#include <vix/cli/modules/ModuleManifest.hpp>

#include <algorithm>

namespace vix::cli::modules
{
  namespace fs = std::filesystem;
  namespace
  {
    bool owner_less(const DependencyOwner &a, const DependencyOwner &b)
    {
      if (a.kind != b.kind) return a.kind == DependencyOwnerKind::Application;
      return a.module < b.module;
    }
  }

  DependencyOwnership build_dependency_ownership(
      const vix::cli::app::AppManifest &app,
      const ModuleGraph &graph,
      const fs::path &projectRoot)
  {
    DependencyOwnership result;
    if (!graph.valid()) { result.error = graph.error(); return result; }
    const DependencyOwner application{};
    for (const std::string &requirement : app.deps)
      result.requirements.push_back({application, DependencySource::Registry, requirement});
    for (const auto &dependency : app.gitDependencies)
    {
      result.requirements.push_back({application, DependencySource::Git, dependency.name});
      if (!dependency.target.empty()) result.links.push_back({application, DependencySource::Git, DependencyVisibility::Private, dependency.target});
      for (const std::string &target : dependency.targets)
        result.links.push_back({application, DependencySource::Git, DependencyVisibility::Private, target});
    }

    std::vector<const ModuleNode *> nodes;
    for (const auto &node : graph.nodes()) nodes.push_back(&node);
    std::sort(nodes.begin(), nodes.end(), [](const ModuleNode *a, const ModuleNode *b) { return a->identity < b->identity; });
    for (const ModuleNode *node : nodes)
    {
      const fs::path path = node->path.is_absolute() ? node->path : projectRoot / node->path;
      const auto loaded = load_module_manifest(path / "vix.module");
      if (!loaded.success())
      {
        if (node->enabled) { result.error = "Enabled module '" + node->name + "' has no valid vix.module: " + loaded.error; return result; }
        continue; // Disabled modules remain declared but a missing manifest is compatible.
      }
      const DependencyOwner owner{DependencyOwnerKind::Module, node->name, node->enabled};
      for (const std::string &requirement : loaded.manifest.registryDependencies)
        result.requirements.push_back({owner, DependencySource::Registry, requirement});
      for (const std::string &target : loaded.manifest.links)
        result.links.push_back({owner, DependencySource::Registry, DependencyVisibility::Private, target});
    }
    std::stable_sort(result.requirements.begin(), result.requirements.end(), [](const auto &a, const auto &b) {
      if (owner_less(a.owner, b.owner)) return true; if (owner_less(b.owner, a.owner)) return false;
      if (a.source != b.source) return a.source < b.source; return a.requirement < b.requirement;
    });
    std::stable_sort(result.links.begin(), result.links.end(), [](const auto &a, const auto &b) {
      if (owner_less(a.owner, b.owner)) return true; if (owner_less(b.owner, a.owner)) return false;
      if (a.source != b.source) return a.source < b.source; return a.target < b.target;
    });
    return result;
  }
}
