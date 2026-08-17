#include <vix/cli/modules/DependencyConstraints.hpp>
#include <vix/cli/util/Semver.hpp>

#include <algorithm>
#include <cctype>
#include <set>

namespace vix::cli::modules
{
  namespace
  {
    std::string trim(std::string value) { const auto b = std::find_if_not(value.begin(), value.end(), [](unsigned char c){ return std::isspace(c); }); const auto e = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c){ return std::isspace(c); }).base(); return b >= e ? "" : std::string(b, e); }
    std::string git_source_subdirectory_identity(const std::string &subdirectory)
    {
      // An omitted subdirectory and "." both select the repository root.
      return subdirectory == "." ? "" : subdirectory;
    }
    bool owner_less(const DependencyOwner &a, const DependencyOwner &b) { if (a.kind != b.kind) return a.kind == DependencyOwnerKind::Application; return a.module < b.module; }
    bool requirement_less(const RegistryRequirement &a, const RegistryRequirement &b) { if (owner_less(a.owner,b.owner)) return true; if (owner_less(b.owner,a.owner)) return false; return a.requestedVersion < b.requestedVersion; }
    bool parse(const std::string &raw, std::string &id, std::string &version)
    {
      std::string value = trim(raw); if (!value.empty() && value.front() == '@') value.erase(value.begin());
      const auto slash = value.find('/'); if (slash == std::string::npos || slash == 0 || slash + 1 == value.size()) return false;
      const auto at = value.find('@', slash + 1); id = trim(value.substr(0, at)); version = at == std::string::npos ? "" : trim(value.substr(at + 1));
      return !id.empty() && (at == std::string::npos || !version.empty());
    }
  }
  DependencyConstraintResult analyze_dependency_constraints(const DependencyOwnership &ownership, const std::map<std::string, std::vector<std::string>> &available)
  {
    DependencyConstraintResult result;
    for (const auto &item : ownership.requirements)
    {
      if (item.source != DependencySource::Registry) continue;
      std::string id, version;
      if (!parse(item.requirement, id, version)) { result.error = "Invalid registry requirement: " + item.requirement; return result; }
      result.declaredRegistryRequirements.push_back({id, version, item.requirement, item.owner});
    }
    std::sort(result.declaredRegistryRequirements.begin(), result.declaredRegistryRequirements.end(), [](const auto &a, const auto &b) { if (a.packageId != b.packageId) return a.packageId < b.packageId; return requirement_less(a,b); });
    for (std::size_t begin = 0; begin < result.declaredRegistryRequirements.size();)
    {
      std::size_t end = begin + 1; while (end < result.declaredRegistryRequirements.size() && result.declaredRegistryRequirements[end].packageId == result.declaredRegistryRequirements[begin].packageId) ++end;
      std::vector<RegistryRequirement> active;
      for (std::size_t i=begin;i<end;++i) if (result.declaredRegistryRequirements[i].owner.active) active.push_back(result.declaredRegistryRequirements[i]);
      if (!active.empty())
      {
        std::vector<RegistryRequirement> unique; std::set<std::string> seen;
        for (const auto &item : active) { const std::string key = std::to_string(static_cast<int>(item.owner.kind)) + "\n" + item.owner.module + "\n" + item.requestedVersion; if (seen.insert(key).second) unique.push_back(item); }
        const auto versionsIt = available.find(active.front().packageId);
        if (versionsIt == available.end()) { result.conflicts.push_back({active.front().packageId, unique, "No available Registry versions were provided."}); }
        else {
          std::vector<std::string> candidates;
          for (const auto &version : versionsIt->second) { bool fits = true; for (const auto &constraint : unique) if (!constraint.requestedVersion.empty() && !vix::cli::util::semver::satisfies(version, constraint.requestedVersion)) { fits = false; break; } if (fits) candidates.push_back(version); }
          if (candidates.empty()) result.conflicts.push_back({active.front().packageId, unique, "No available version satisfies all active requirements."});
          else result.resolvedRegistry.push_back({active.front().packageId, vix::cli::util::semver::findLatest(candidates), unique});
        }
      }
      begin = end;
    }
    return result;
  }
  std::optional<std::string> registry_dependency_identity(const std::string &requirement)
  {
    std::string id, version;
    return parse(requirement, id, version) ? std::optional<std::string>(id) : std::nullopt;
  }
  GitConstraintResult analyze_git_constraints(std::vector<GitDependencyConstraint> constraints)
  {
    GitConstraintResult result;
    std::sort(constraints.begin(), constraints.end(), [](const auto &a, const auto &b) {
      if (a.build.repository != b.build.repository) return a.build.repository < b.build.repository;
      if (git_source_subdirectory_identity(a.build.subdirectory) != git_source_subdirectory_identity(b.build.subdirectory)) return git_source_subdirectory_identity(a.build.subdirectory) < git_source_subdirectory_identity(b.build.subdirectory);
      return owner_less(a.owner, b.owner);
    });
    for (std::size_t begin=0; begin<constraints.size();)
    {
      std::size_t end=begin+1; while (end<constraints.size() && constraints[end].build.repository == constraints[begin].build.repository && git_source_subdirectory_identity(constraints[end].build.subdirectory) == git_source_subdirectory_identity(constraints[begin].build.subdirectory)) ++end;
      std::vector<GitDependencyConstraint> active; for (std::size_t i=begin;i<end;++i) if (constraints[i].owner.active) active.push_back(constraints[i]);
      if (active.size() > 1)
      {
        const auto &base = active.front().build;
        for (std::size_t i=1;i<active.size();++i)
        {
          const auto &next = active[i].build;
          if (next.revision != base.revision) { result.conflicts.push_back({base.repository, active, "Resolved revisions differ."}); break; }
          if (next.cmakeOptions != base.cmakeOptions) { result.conflicts.push_back({base.repository, active, "CMake option values differ."}); break; }
        }
      }
      begin=end;
    }
    return result;
  }
  GitConstraintResult analyze_owned_git_constraints(const DependencyOwnership &ownership)
  {
    std::vector<GitDependencyConstraint> constraints;
    for (const auto &item : ownership.gitDependencies)
    {
      const auto &d = item.dependency;
      const std::string revision = !d.rev.empty() ? d.rev : (!d.tag.empty() ? "tag:" + d.tag : (!d.branch.empty() ? "branch:" + d.branch : ""));
      constraints.push_back({item.owner, {d.git, revision, d.subdirectory, d.cmakeOptions, d.targets}});
    }
    return analyze_git_constraints(std::move(constraints));
  }
}
