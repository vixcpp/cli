#include <vix/cli/project/native/PackageBuildGraph.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <variant>

#include <vix/cli/project/native/Lockfile.hpp>
#include <vix/cli/project/native/ProjectLockState.hpp>
#include <vix/cli/project/native/TomlParser.hpp>
#include <vix/cli/project/native/Validation.hpp>

namespace vix::cli::project::native
{
  namespace
  {
    using NodeMap = std::map<std::string, const LockNode *>;
    using RootMap = std::map<std::string, std::string>;
    using MaterializedMap = std::map<std::string, std::filesystem::path>;
    bool safe_cmake_name(std::string_view value)
    {
      if (value.empty() || value.front() == '-') return false;
      return std::all_of(value.begin(), value.end(), [](unsigned char c)
      { return std::isalnum(c) || c == '_' || c == '.' || c == '+' || c == ':' || c == '-'; });
    }

    bool safe_target_name(std::string_view value)
    {
      return !value.empty() && value.front() != '-' && std::all_of(value.begin(), value.end(),
        [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '.' || c == '+' || c == '-'; });
    }

    std::string bracket(std::string_view value)
    {
      std::string equals;
      while (value.find("]" + equals + "]") != std::string_view::npos) equals.push_back('=');
      return "[" + equals + "[" + std::string{value} + "]" + equals + "]";
    }

    void issue(PackageBuildGraphResult &result, PackageBuildGraphIssueCode code,
      std::string subject, std::string message)
    { result.issues.push_back({code, std::move(subject), std::move(message)}); }

    const LockNode *find_node(const NodeMap &nodes, const std::string &id)
    {
      const auto found = nodes.find(id);
      return found == nodes.end() ? nullptr : found->second;
    }

    std::string kind_name(TargetKind kind)
    {
      switch (kind)
      {
        case TargetKind::executable: return "EXECUTABLE";
        case TargetKind::static_library: return "STATIC";
        case TargetKind::shared_library: return "SHARED";
        case TargetKind::interface_library: return "INTERFACE";
      }
      return {};
    }

    std::string visibility_name(UsageVisibility visibility)
    {
      switch (visibility)
      {
        case UsageVisibility::private_: return "PRIVATE";
        case UsageVisibility::public_: return "PUBLIC";
        case UsageVisibility::interface_: return "INTERFACE";
      }
      return {};
    }

    std::string standard_name(CxxStandard standard)
    { return standard == CxxStandard::cxx20 ? "20" : "23"; }

    struct PackageInfo
    {
      std::filesystem::path root;
      PackageContract contract;
      std::optional<NativeProject> project;
      std::map<std::string, std::string> exports;
    };

    bool matches_contract_identity(const LockNode &node, const PackageContractIdentity &identity)
    {
      if (const auto *registry = std::get_if<RegistryLockNode>(&node))
      {
        const auto *declared = std::get_if<RegistryPackageContractIdentity>(&identity);
        return declared && declared->identity == registry->identity && declared->version == registry->version;
      }
      return std::holds_alternative<GitPackageContractIdentity>(identity);
    }

    const std::vector<LockDependency> &dependencies(const LockNode &node)
    { return std::visit([](const auto &value) -> const auto & { return value.dependencies; }, node); }

    bool requirement_matches_node(const PackageRequirement &requirement, const LockNode &child)
    {
      if (const auto *registryRequirement = std::get_if<RegistryRequirement>(&requirement.source))
      {
        const auto *registry = std::get_if<RegistryLockNode>(&child);
        if (!registry || registry->identity != registryRequirement->identity) return false;
        return std::visit([&](const auto &versionRequirement)
        {
          using Requirement = std::decay_t<decltype(versionRequirement)>;
          if constexpr (std::is_same_v<Requirement, ExactVersion>)
            return registry->version == versionRequirement.version;
          else
          {
            const auto &minimum = versionRequirement.version;
            if (registry->version.prerelease || minimum.prerelease || registry->version.major != minimum.major ||
                std::tie(registry->version.major, registry->version.minor, registry->version.patch) <
                  std::tie(minimum.major, minimum.minor, minimum.patch)) return false;
            if (minimum.major != 0) return true;
            if (minimum.minor != 0) return registry->version.minor == minimum.minor;
            return registry->version.minor == 0 && registry->version.patch == minimum.patch;
          }
        }, registryRequirement->version);
      }
      const auto &gitRequirement = std::get<GitRequirement>(requirement.source);
      const auto *git = std::get_if<GitLockNode>(&child);
      return git && git->repositoryRoot && gitRequirement.repositoryRoot &&
        git->normalizedRepository == gitRequirement.normalizedRepository;
    }

    bool validate_declared_dependencies(const std::vector<PackageRequirement> &declared,
      const LockNode &node, const NodeMap &nodes)
    {
      if (declared.size() != dependencies(node).size()) return false;
      std::map<std::string, std::string> aliases;
      for (const auto &dependency : dependencies(node)) aliases.emplace(dependency.alias, dependency.node);
      for (const auto &requirement : declared)
      {
        const auto edge = aliases.find(requirement.alias);
        if (edge == aliases.end()) return false;
        const auto *child = find_node(nodes, edge->second);
        if (!child || !requirement_matches_node(requirement, *child)) return false;
      }
      return true;
    }

    void append_target(std::ostringstream &out, const NativeTarget &target,
      const std::filesystem::path &root, std::string_view generatedName,
      std::optional<CxxStandard> projectStandard, PackageBuildGraphResult &result)
    {
      if (!safe_target_name(generatedName))
      {
        issue(result, PackageBuildGraphIssueCode::target_collision, std::string{generatedName},
          "target name cannot be represented safely in CMake");
        return;
      }
      out << (target.kind == TargetKind::executable ? "add_executable(" : "add_library(")
          << generatedName;
      if (target.kind != TargetKind::executable) out << " " << kind_name(target.kind);
      for (const auto &source : target.sources)
        out << "\n  " << bracket((root / std::filesystem::path{source}).lexically_normal().string());
      out << ")\n";

      const auto standard = target.cxxStandard ? target.cxxStandard : projectStandard;
      if (standard)
        out << "target_compile_features(" << generatedName << " " << visibility_name(
          target.kind == TargetKind::executable ? UsageVisibility::private_ :
            (target.kind == TargetKind::interface_library ? UsageVisibility::interface_ : UsageVisibility::public_))
          << " cxx_std_" << standard_name(*standard) << ")\n";

      for (const auto &group : target.includes)
        for (const auto &path : group.paths)
          out << "target_include_directories(" << generatedName << " " << visibility_name(group.visibility)
              << " " << bracket((root / std::filesystem::path{path}).lexically_normal().string()) << ")\n";
      for (const auto &group : target.definitions)
        for (const auto &value : group.values)
          out << "target_compile_definitions(" << generatedName << " " << visibility_name(group.visibility)
              << " " << bracket(value) << ")\n";
    }
  }

  PackageBuildGraphResult generate_package_build_graph(const NativeProject &project,
    const std::filesystem::path &projectRoot, const Lockfile &lockfile,
    const std::vector<MaterializedLockedPackage> &packages)
  {
    PackageBuildGraphResult result;
    try
    {
    const auto projectValidation = validate_native_project(project);
    if (!projectValidation.valid())
    {
      issue(result, PackageBuildGraphIssueCode::invalid_project, project.name,
        "native project semantic validation failed");
      return result;
    }
    const auto root = std::filesystem::absolute(projectRoot).lexically_normal();
    if (!validate_native_project_paths(project, root).valid())
    {
      issue(result, PackageBuildGraphIssueCode::invalid_project, root.string(),
        "native project paths are invalid or missing");
      return result;
    }
    const bool hasPackageRequirements = !project.packages.empty();
    const bool hasValidPackageGraph = hasPackageRequirements
      ? validate_lockfile(lockfile).valid() && validate_lockfile_integrity(lockfile).valid()
      : lockfile.roots.empty() && lockfile.nodes.empty() && packages.empty() &&
          validate_lockfile_integrity(lockfile).valid();
    if (!hasValidPackageGraph)
    {
      issue(result, PackageBuildGraphIssueCode::invalid_lockfile, "vix.lock",
        "locked package graph is semantically invalid or fails integrity validation");
      return result;
    }
    if (project_requirements_digest(project) != lockfile.rootRequirementsDigest)
    {
      issue(result, PackageBuildGraphIssueCode::invalid_lockfile, "vix.lock",
        "lock root requirements do not match the current native project");
      return result;
    }

    NodeMap nodes;
    for (const auto &node : lockfile.nodes)
      std::visit([&](const auto &value) { nodes.emplace(value.id, &node); }, node);
    RootMap roots;
    std::set<std::string> reachable;
    std::vector<std::string> pending;
    for (const auto &lockRoot : lockfile.roots)
    {
      roots.emplace(lockRoot.alias, lockRoot.node);
      pending.push_back(lockRoot.node);
    }
    while (!pending.empty())
    {
      auto id = std::move(pending.back()); pending.pop_back();
      if (!reachable.insert(id).second) continue;
      const auto *node = find_node(nodes, id);
      if (!node) { issue(result, PackageBuildGraphIssueCode::invalid_lockfile, id, "locked dependency node is missing"); continue; }
      for (const auto &dependency : dependencies(*node)) pending.push_back(dependency.node);
    }

    MaterializedMap materialized;
    for (const auto &entry : packages)
    {
      if (!reachable.contains(entry.nodeId))
        issue(result, PackageBuildGraphIssueCode::unexpected_materialization, entry.nodeId,
          "materialized package is not reachable from the lock roots");
      const auto *node = find_node(nodes, entry.nodeId);
      if (!node || std::visit([&](const auto &value) { return value.contentDigest != entry.content.digest(); }, *node))
        issue(result, PackageBuildGraphIssueCode::invalid_lockfile, entry.nodeId,
          "materialized content identity does not match the lock node digest");
      if (!materialized.emplace(entry.nodeId, std::filesystem::absolute(entry.root).lexically_normal()).second)
        issue(result, PackageBuildGraphIssueCode::unexpected_materialization, entry.nodeId,
          "duplicate materialization supplied for one lock node");
    }
    for (const auto &id : reachable)
      if (!materialized.contains(id)) issue(result, PackageBuildGraphIssueCode::missing_materialization,
        id, "locked package has no P10R-A materialized root");
    if (!result.issues.empty()) return result;

    std::map<std::string, PackageInfo> info;
    std::set<std::string> cmakeTargets;
    std::map<std::string, std::string> cmakeTargetOwners;
    std::map<std::string, std::string> cmakeConfigOwners;
    std::set<std::string> generatedTargets;
    for (const auto &target : project.targets)
    {
      if (!safe_target_name(target.name) || !generatedTargets.insert(target.name).second)
        issue(result, PackageBuildGraphIssueCode::target_collision, target.name, "root target name is invalid or collides");
    }

    for (const auto &id : reachable)
    {
      const auto *node = find_node(nodes, id);
      const auto packageRoot = materialized.at(id);
      std::error_code rootError;
      if (!std::filesystem::is_directory(packageRoot, rootError) || rootError)
      {
        issue(result, PackageBuildGraphIssueCode::missing_materialization, id,
          "supplied materialized package root is missing or is not a directory");
        continue;
      }
      const auto contractPath = packageRoot / "vix.package.toml";
      const auto parsedContract = parse_package_contract_file(contractPath);
      if (!std::filesystem::exists(contractPath))
      {
        issue(result, PackageBuildGraphIssueCode::missing_package_metadata, id,
          "materialized package is missing vix.package.toml");
        continue;
      }
      if (!parsedContract.valid())
      {
        issue(result, PackageBuildGraphIssueCode::invalid_package_contract, id,
          "materialized vix.package.toml could not be parsed");
        continue;
      }
      const auto &contract = *parsedContract.value;
      if (!validate_package_contract(contract).valid() || !matches_contract_identity(*node, contract.identity) ||
          !validate_declared_dependencies(contract.packages, *node, nodes))
      {
        issue(result, PackageBuildGraphIssueCode::invalid_package_contract, id,
          "package contract does not match the exact locked identity or dependency aliases");
        continue;
      }
      PackageInfo package{packageRoot, contract, std::nullopt, {}};
      if (const auto *nativeIntegration = std::get_if<NativeVixIntegration>(&contract.integration))
      {
        (void)nativeIntegration;
        const auto nativeManifestPath = packageRoot / "vix.toml";
        if (!std::filesystem::exists(nativeManifestPath))
        {
          issue(result, PackageBuildGraphIssueCode::missing_package_metadata, id,
            "native-vix package is missing vix.toml");
          continue;
        }
        const auto parsedProject = parse_native_manifest_file(nativeManifestPath);
        if (!parsedProject.valid() ||
            !validate_native_project_paths(*parsedProject.value, packageRoot).valid() ||
            !validate_native_package(*parsedProject.value, contract, packageRoot).valid())
        {
          issue(result, PackageBuildGraphIssueCode::invalid_native_package, id,
            "native-vix package project or contract validation failed");
          continue;
        }
        package.project = *parsedProject.value;
        for (const auto &target : package.project->targets)
        {
          if (!safe_target_name(target.name) || !generatedTargets.insert(target.name).second)
            issue(result, PackageBuildGraphIssueCode::target_collision, target.name,
              "package target name collides with another target in the locked build graph");
        }
        for (const auto &exported : contract.exports)
        {
          const auto *mapping = std::get_if<NativeExportMapping>(&exported.mapping);
          if (!mapping) { issue(result, PackageBuildGraphIssueCode::invalid_package_contract, id,
              "native-vix export has a non-native mapping"); continue; }
          if (!package.exports.emplace(exported.name, mapping->projectTarget).second)
            issue(result, PackageBuildGraphIssueCode::invalid_package_contract, exported.name, "duplicate export name");
        }
      }
      else
      {
        const auto &integration = std::get<CMakeConfigIntegration>(contract.integration);
        const auto [configOwner, configInserted] = cmakeConfigOwners.emplace(integration.configName, id);
        if (!configInserted && configOwner->second != id)
          issue(result, PackageBuildGraphIssueCode::target_collision, integration.configName,
            "distinct locked packages require the same CMake config name");
        for (const auto &exported : contract.exports)
        {
          const auto *mapping = std::get_if<CMakeExportMapping>(&exported.mapping);
          if (!mapping || !safe_cmake_name(mapping->cmakeTarget) || !safe_target_name(integration.configName))
          {
            issue(result, PackageBuildGraphIssueCode::invalid_package_contract, exported.name,
              "CMake-config export/config name is invalid");
            continue;
          }
          if (!package.exports.emplace(exported.name, mapping->cmakeTarget).second)
            issue(result, PackageBuildGraphIssueCode::invalid_package_contract, exported.name, "duplicate export name");
          const auto [owner, inserted] = cmakeTargetOwners.emplace(mapping->cmakeTarget, id);
          if (!inserted && owner->second != id)
            issue(result, PackageBuildGraphIssueCode::target_collision, mapping->cmakeTarget,
              "distinct locked packages declare the same public CMake target");
          cmakeTargets.insert(mapping->cmakeTarget);
        }
      }
      info.emplace(id, std::move(package));
    }
    for (const auto &name : cmakeTargets)
      if (generatedTargets.contains(name)) issue(result, PackageBuildGraphIssueCode::target_collision, name,
        "declared CMake package target collides with a generated Vix target");
    if (!result.issues.empty()) return result;

    std::ostringstream out;
    out << "cmake_minimum_required(VERSION 3.20)\nproject(" << bracket(project.name)
        << " LANGUAGES CXX)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n";

    for (const auto &[id, package] : info)
    {
      (void)id;
      if (std::holds_alternative<CMakeConfigIntegration>(package.contract.integration))
        out << "list(PREPEND CMAKE_PREFIX_PATH " << bracket(package.root.string()) << ")\n";
    }

    // Import CMake-config targets before any generated target links to them.
    std::set<std::string> configs;
    for (const auto &[id, package] : info)
    {
      (void)id;
      if (const auto *integration = std::get_if<CMakeConfigIntegration>(&package.contract.integration))
        configs.insert(integration->configName);
    }
    for (const auto &config : configs) out << "find_package(" << config << " CONFIG REQUIRED)\n";
    for (const auto &[id, package] : info)
    {
      if (const auto *integration = std::get_if<CMakeConfigIntegration>(&package.contract.integration))
      {
        for (const auto &[exportName, target] : package.exports)
        {
          (void)exportName;
          out << "if(NOT TARGET " << target << ")\n  message(FATAL_ERROR " << bracket(
            "package config " + integration->configName + " did not declare required target " + target)
            << ")\nendif()\n";
        }
      }
      (void)id;
    }

    for (const auto &[id, package] : info)
      if (package.project)
        for (const auto &target : package.project->targets)
          append_target(out, target, package.root, target.name, package.project->cxxStandard, result);
    for (const auto &target : project.targets) append_target(out, target, root, target.name, project.cxxStandard, result);
    if (!result.issues.empty()) return result;

    auto package_target = [&](const std::string &nodeId, const std::string &alias, const std::string &exportName)
      -> std::optional<std::string>
    {
      const auto *node = find_node(nodes, nodeId);
      if (!node) return std::nullopt;
      const auto &packageDeps = dependencies(*node);
      const auto dependency = std::find_if(packageDeps.begin(), packageDeps.end(), [&](const auto &item)
        { return item.alias == alias; });
      if (dependency == packageDeps.end()) return std::nullopt;
      const auto package = info.find(dependency->node);
      if (package == info.end()) return std::nullopt;
      const auto exported = package->second.exports.find(exportName);
      return exported == package->second.exports.end() ? std::nullopt : std::optional<std::string>{exported->second};
    };

    auto append_uses = [&](const NativeProject &nativeProject, const std::string &nodeId,
      bool rootProject, const std::filesystem::path &packageRoot)
    {
      (void)packageRoot;
      for (const auto &target : nativeProject.targets)
      {
        const auto from = target.name;
        for (const auto &use : target.uses)
        {
          const auto to = use.target;
          out << "target_link_libraries(" << from << " " << visibility_name(use.visibility) << " " << to << ")\n";
        }
        for (const auto &use : target.packageUses)
        {
          std::optional<std::string> to;
          if (rootProject)
          {
            const auto rootNode = roots.find(use.requirementAlias);
            if (rootNode != roots.end())
            {
              const auto package = info.find(rootNode->second);
              if (package != info.end())
              {
                const auto exported = package->second.exports.find(use.exportName);
                if (exported != package->second.exports.end()) to = exported->second;
              }
            }
          }
          else to = package_target(nodeId, use.requirementAlias, use.exportName);
          if (!to) issue(result, PackageBuildGraphIssueCode::missing_declared_target,
            from + ":" + use.requirementAlias + ":" + use.exportName,
            "locked package export referenced by target use is unavailable");
          else out << "target_link_libraries(" << from << " " << visibility_name(use.visibility) << " " << *to << ")\n";
        }
      }
    };
    for (const auto &[id, package] : info)
      if (package.project) append_uses(*package.project, id, false, package.root);
    append_uses(project, {}, true, root);

    if (!project.tests.empty())
    {
      out << "enable_testing()\n";
      std::set<std::string> testNames;
      std::set<std::string> rootTargetNames;
      for (const auto &target : project.targets) rootTargetNames.insert(target.name);
      for (const auto &test : project.tests)
      {
        if (test.name.empty() || !testNames.insert(test.name).second ||
            !rootTargetNames.contains(test.target))
        {
          issue(result, PackageBuildGraphIssueCode::invalid_project, test.name,
            "test registration is duplicate or references an unavailable root target");
          continue;
        }
        out << "add_test(NAME " << bracket(test.name) << " COMMAND $<TARGET_FILE:"
            << test.target << ">)\n";
      }
    }

    if (!result.issues.empty()) return result;
    result.cmakeLists = out.str();
    return result;
    }
    catch (const std::exception &error)
    {
      issue(result, PackageBuildGraphIssueCode::filesystem_failure, "build graph",
        std::string{"filesystem or metadata inspection failed: "} + error.what());
      return result;
    }
  }
} // namespace vix::cli::project::native
