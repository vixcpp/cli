#include <vix/cli/modules/DependencyConstraints.hpp>
#include <cassert>
#include <map>

using namespace vix::cli::modules;
static DependencyOwner app() { return {}; }
static DependencyOwner mod(const std::string &name, bool active = true) { return {DependencyOwnerKind::Module, name, active}; }
static DependencyOwnership ownership(std::initializer_list<OwnedDependencyRequirement> items) { DependencyOwnership out; out.requirements = items; return out; }
static std::map<std::string, std::vector<std::string>> versions() { return {{"gk/json", {"1.0.0", "1.5.0", "1.8.0", "2.0.0"}}}; }
int main()
{
  const auto registry = DependencySource::Registry;
  { auto r = analyze_dependency_constraints(ownership({{app(),registry,"gk/json@^1.0"}}), versions()); assert(r.success() && r.resolvedRegistry[0].version == "1.8.0"); }
  { auto r = analyze_dependency_constraints(ownership({{app(),registry,"gk/json@^1.0"},{mod("auth"),registry,"gk/json@^1.2"}}), versions()); assert(r.success() && r.resolvedRegistry[0].version == "1.8.0"); }
  { auto r = analyze_dependency_constraints(ownership({{app(),registry,"gk/json@1.5.0"},{mod("auth"),registry,"gk/json@^1.0"}}), versions()); assert(r.success() && r.resolvedRegistry[0].version == "1.5.0"); }
  { auto r = analyze_dependency_constraints(ownership({{mod("auth"),registry,"gk/json@^1"},{mod("billing"),registry,"gk/json@^2"}}), versions()); assert(!r.success() && r.conflicts.size() == 1 && r.conflicts[0].requirements.size() == 2); }
  { auto r = analyze_dependency_constraints(ownership({{app(),registry,"gk/json@1.0.0"},{mod("auth"),registry,"gk/json@2.0.0"}}), versions()); assert(!r.success()); }
  { auto r = analyze_dependency_constraints(ownership({{app(),registry,"gk/json@^1"},{mod("legacy",false),registry,"gk/json@^2"}}), versions()); assert(r.success() && r.declaredRegistryRequirements.size() == 2); }
  { auto r = analyze_dependency_constraints(ownership({{mod("auth"),registry,"gk/json@^1"},{mod("auth"),registry,"gk/json@^1"}}), versions()); assert(r.success() && r.resolvedRegistry[0].requirements.size() == 1); }
  { auto r = analyze_dependency_constraints(ownership({{mod("auth"),registry,"gk/json@^1"},{mod("auth"),registry,"gk/json@^2"}}), versions()); assert(!r.success()); }
  { auto r = analyze_dependency_constraints(ownership({{mod("z"),registry,"gk/json@^1"},{app(),registry,"gk/json@^1"},{mod("a"),registry,"gk/json@^1"}}), versions()); assert(r.success()); assert(r.declaredRegistryRequirements[0].owner.kind == DependencyOwnerKind::Application); assert(r.declaredRegistryRequirements[1].owner.module == "a"); }
  assert(registry_dependency_identity("@gk/json@^1").value() == "gk/json");
  GitDependencyConstraint ga{mod("auth"), {"https://example.invalid/repo.git", "abc", "", {{"OPT", "ON"}}, {"repo::x"}}};
  GitDependencyConstraint gb{mod("billing"), {"https://example.invalid/repo.git", "abc", "", {{"OPT", "ON"}}, {"repo::x"}}};
  assert(analyze_git_constraints({ga, gb}).success());
  gb.build.revision = "def"; assert(!analyze_git_constraints({ga, gb}).success());
  gb.build.revision = "abc"; gb.build.cmakeOptions = {{"OPT", "OFF"}}; assert(!analyze_git_constraints({ga, gb}).success());
  gb.build.repository = "https://example.invalid/other.git"; assert(analyze_git_constraints({ga, gb}).success());
}
