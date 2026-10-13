#include <vix/cli/project/native/Validation.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

using namespace vix::cli::project::native;

namespace
{
  struct PackageFixture
  {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
      ("vix-native-semantic-model-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    PackageFixture()
    {
      std::filesystem::create_directories(root / "include");
    }
    ~PackageFixture() { std::filesystem::remove_all(root); }
  };

  PackageRequirement registry_requirement(std::string alias = "fmt", VersionRequirement version = CompatibleVersion{Version{11, 0, 0, {}}})
  {
    return {std::move(alias), RegistryRequirement{{"vix", "fmtlib", "fmt"}, std::move(version)}};
  }

  NativeTarget executable(std::string name, std::string source = "main.cpp")
  {
    NativeTarget target;
    target.name = std::move(name);
    target.kind = TargetKind::executable;
    target.sources.push_back(std::move(source));
    return target;
  }

  NativeProject minimal_project()
  {
    NativeProject project;
    project.name = "demo";
    project.targets.push_back(executable("app"));
    return project;
  }

  bool has_code(const ValidationResult &result, ValidationCode code)
  {
    for (const auto &issue : result.issues) if (issue.code == code) return true;
    return false;
  }

  PackageContract native_contract()
  {
    PackageContract contract;
    contract.identity = RegistryPackageContractIdentity{{"vix", "example", "netkit"}, Version{1, 2, 0, {}}};
    contract.integration = NativeVixIntegration{{"include"}};
    contract.exports.push_back({"core", NativeExportMapping{"core"}});
    return contract;
  }

  PackageContract cmake_contract()
  {
    PackageContract contract;
    contract.identity = RegistryPackageContractIdentity{{"vix", "example", "fmt"}, Version{1, 0, 0, {}}};
    contract.integration = CMakeConfigIntegration{"fmt"};
    contract.exports.push_back({"fmt", CMakeExportMapping{"fmt::fmt"}});
    return contract;
  }

  void native_manifest_cases()
  {
    auto project = minimal_project();
    project.cxxStandard = CxxStandard::cxx23;
    assert(validate_native_project(project).valid());

    auto multi = minimal_project();
    NativeTarget core;
    core.name = "core";
    core.kind = TargetKind::static_library;
    core.sources = {"src/core.cpp"};
    core.includes.push_back({UsageVisibility::public_, {"include"}});
    multi.targets.insert(multi.targets.begin(), core);
    multi.targets[1].uses.push_back({UsageVisibility::private_, "core"});
    core.definitions.push_back({UsageVisibility::public_, {"API_VERSION=3"}});
    multi.targets[0] = core;
    multi.packages.push_back(registry_requirement());
    multi.targets[1].packageUses.push_back({UsageVisibility::private_, "fmt", "fmt"});
    multi.tests = {{"core", "app"}, {"core", "app"}};
    assert(validate_native_project(multi).valid());

    auto interfaceProject = NativeProject{};
    interfaceProject.name = "api";
    NativeTarget api;
    api.name = "api";
    api.kind = TargetKind::interface_library;
    api.includes.push_back({UsageVisibility::interface_, {"include"}});
    interfaceProject.targets.push_back(api);
    assert(validate_native_project(interfaceProject).valid());

    auto invalid = minimal_project();
    invalid.targets.push_back(executable("app", "other.cpp"));
    assert(has_code(validate_native_project(invalid), ValidationCode::duplicate_target));

    invalid = NativeProject{}; invalid.name = "demo";
    NativeTarget noSource; noSource.name = "lib"; noSource.kind = TargetKind::static_library;
    invalid.targets.push_back(noSource);
    assert(has_code(validate_native_project(invalid), ValidationCode::invalid_target_sources));

    invalid = NativeProject{}; invalid.name = "demo";
    api.sources = {"api.cpp"}; invalid.targets.push_back(api);
    assert(has_code(validate_native_project(invalid), ValidationCode::invalid_target_sources));

    invalid = minimal_project(); invalid.targets.front().uses.push_back({UsageVisibility::private_, "missing"});
    assert(has_code(validate_native_project(invalid), ValidationCode::unknown_target));
    invalid.targets.front().uses.back().target = "app";
    assert(has_code(validate_native_project(invalid), ValidationCode::self_dependency));

    invalid = NativeProject{}; invalid.name = "demo";
    NativeTarget a; a.name = "a"; a.kind = TargetKind::static_library; a.sources = {"a.cpp"}; a.uses = {{UsageVisibility::private_, "b"}};
    NativeTarget b; b.name = "b"; b.kind = TargetKind::static_library; b.sources = {"b.cpp"}; b.uses = {{UsageVisibility::private_, "a"}};
    invalid.targets = {a, b};
    assert(has_code(validate_native_project(invalid), ValidationCode::target_cycle));

    invalid = minimal_project(); invalid.targets.front().uses.push_back({UsageVisibility::public_, "x"});
    assert(has_code(validate_native_project(invalid), ValidationCode::invalid_visibility));
    invalid = NativeProject{}; invalid.name = "demo"; api.includes = {{UsageVisibility::private_, {"include"}}}; invalid.targets = {api};
    assert(has_code(validate_native_project(invalid), ValidationCode::invalid_visibility));

    invalid = minimal_project(); invalid.targets.front().sources = {"a/../main.cpp", "main.cpp"};
    assert(has_code(validate_native_project(invalid), ValidationCode::duplicate_source));
    auto crossTargetSources = minimal_project(); crossTargetSources.targets.push_back(executable("other", "main.cpp"));
    assert(validate_native_project(crossTargetSources).valid());

    invalid = minimal_project(); invalid.tests = {{"run", "absent"}};
    assert(has_code(validate_native_project(invalid), ValidationCode::invalid_test));
    invalid = minimal_project(); NativeTarget library; library.name = "lib"; library.kind = TargetKind::static_library; library.sources = {"lib.cpp"}; invalid.targets.push_back(library); invalid.tests = {{"run", "lib"}};
    assert(has_code(validate_native_project(invalid), ValidationCode::invalid_test));

    invalid = minimal_project(); invalid.cxxStandard = static_cast<CxxStandard>(99);
    invalid.targets.front().cxxStandard = static_cast<CxxStandard>(99);
    // Enum values are schema values; defensive validation rejects out-of-domain in-memory values.
    assert(has_code(validate_native_project(invalid), ValidationCode::invalid_cxx_standard));

    invalid = minimal_project(); invalid.targets.front().packageUses.push_back({UsageVisibility::private_, "unknown", "fmt"});
    assert(has_code(validate_native_project(invalid), ValidationCode::unknown_package_alias));
    invalid = minimal_project(); invalid.packages = {registry_requirement(), registry_requirement()};
    assert(has_code(validate_native_project(invalid), ValidationCode::duplicate_package_alias));
    invalid = minimal_project(); invalid.packages = {registry_requirement("Bad")};
    assert(has_code(validate_native_project(invalid), ValidationCode::invalid_package_alias));
    invalid = minimal_project(); invalid.targets.front().definitions = {{UsageVisibility::private_, {"-DFEATURE_X"}}};
    assert(has_code(validate_native_project(invalid), ValidationCode::invalid_compile_definition));
    invalid = minimal_project(); invalid.packages = {registry_requirement("exp", ExactVersion{Version{1, 0, 0, "beta.2"}})};
    assert(validate_native_project(invalid).valid());
    invalid.packages.front() = registry_requirement("exp", CompatibleVersion{Version{1, 0, 0, "beta.2"}});
    assert(has_code(validate_native_project(invalid), ValidationCode::invalid_requirement));
  }

  void package_contract_cases()
  {
    PackageFixture fixture;
    auto contract = native_contract();
    assert(validate_package_contract(contract).valid());
    contract.identity = GitPackageContractIdentity{};
    assert(validate_package_contract(contract).valid());
    assert(validate_package_contract(cmake_contract()).valid());

    contract = native_contract(); contract.exports.push_back(contract.exports.front());
    assert(has_code(validate_package_contract(contract), ValidationCode::duplicate_export));
    contract = native_contract(); contract.packages = {registry_requirement(), registry_requirement()};
    assert(has_code(validate_package_contract(contract), ValidationCode::duplicate_package_alias));
    contract = native_contract(); contract.exports.front().mapping = CMakeExportMapping{"x::x"};
    assert(has_code(validate_package_contract(contract), ValidationCode::invalid_export_mapping));
    contract = cmake_contract(); contract.exports.front().mapping = NativeExportMapping{"api"};
    assert(has_code(validate_package_contract(contract), ValidationCode::invalid_export_mapping));
    contract = cmake_contract(); std::get<CMakeConfigIntegration>(contract.integration).configName.clear();
    assert(has_code(validate_package_contract(contract), ValidationCode::invalid_package_contract));

    // The discriminated integration model has no representation for include-roots on cmake-config.
    contract = cmake_contract();
    assert(!std::holds_alternative<NativeVixIntegration>(contract.integration));

    auto project = NativeProject{}; project.name = "demo";
    NativeTarget core; core.name = "core"; core.kind = TargetKind::static_library; core.sources = {"src/core.cpp"};
    project.targets.push_back(core);
    contract = native_contract();
    contract.exports.front().mapping = NativeExportMapping{"app"};
    project.targets.push_back(executable("app"));
    assert(has_code(validate_native_package(project, contract, fixture.root), ValidationCode::invalid_export_mapping));
    contract.exports.front().mapping = NativeExportMapping{"missing"};
    assert(has_code(validate_native_package(project, contract, fixture.root), ValidationCode::invalid_export_mapping));

    contract.exports.front().mapping = NativeExportMapping{"core"};
    project.packages = {registry_requirement("tls")};
    project.targets.front().packageUses.push_back({UsageVisibility::public_, "tls", "ssl"});
    assert(has_code(validate_native_package(project, contract, fixture.root), ValidationCode::unknown_package_alias));
    contract.packages = {registry_requirement("tls")};
    assert(validate_native_package(project, contract, fixture.root).valid());
    contract.packages.push_back(registry_requirement("unused"));
    assert(validate_package_contract(contract).valid());

    project.targets.front().includes = {{UsageVisibility::public_, {"generated-public"}}};
    assert(has_code(validate_native_package(project, contract, fixture.root), ValidationCode::invalid_include_root));
    project.targets.front().includes = {{UsageVisibility::private_, {"private-only"}}};
    assert(!has_code(validate_native_package(project, contract, fixture.root), ValidationCode::invalid_include_root));
  }

  Lockfile valid_lock()
  {
    Lockfile lock;
    lock.rootRequirementsDigest = "sha256:" + std::string(64, 'a');
    RegistryLockNode node;
    node.id = "sha256:" + std::string(64, 'b');
    node.identity = {"vix", "fmtlib", "fmt"};
    node.version = {11, 1, 2, {}};
    node.contentDigest = "sha256:" + std::string(64, 'c');
    lock.nodes.push_back(node);
    lock.roots.push_back({"fmt", NormalizedDeclaredRegistry{{"vix", "fmtlib", "fmt"}, CompatibleVersion{Version{11, 0, 0, {}}}}, node.id});
    return lock;
  }

  void lock_cases()
  {
    auto lock = valid_lock();
    assert(validate_lockfile(lock).valid());

    auto gitLock = lock;
    GitLockNode git;
    git.id = "sha256:" + std::string(64, 'd'); git.normalizedRepository = "https://example.invalid/org/protocol.git";
    git.commit = std::string(40, 'e'); git.contentDigest = "sha512:" + std::string(128, 'f');
    gitLock.nodes.push_back(git);
    gitLock.roots.push_back({"protocol", NormalizedDeclaredGit{git.normalizedRepository, true, TagRevision{"v1.2.0"}}, git.id});
    assert(validate_lockfile(gitLock).valid());

    auto invalid = lock; invalid.roots.push_back(invalid.roots.front());
    assert(has_code(validate_lockfile(invalid), ValidationCode::duplicate_lock_root));
    invalid = lock; invalid.nodes.push_back(invalid.nodes.front());
    assert(has_code(validate_lockfile(invalid), ValidationCode::duplicate_lock_node));
    invalid = lock; invalid.roots.front().node = "missing";
    assert(has_code(validate_lockfile(invalid), ValidationCode::unknown_lock_node));
    invalid = lock; std::get<RegistryLockNode>(invalid.nodes.front()).dependencies.push_back({"child", "missing"});
    assert(has_code(validate_lockfile(invalid), ValidationCode::unknown_lock_node));
    invalid = lock; auto &node = std::get<RegistryLockNode>(invalid.nodes.front()); node.dependencies = {{"child", node.id}, {"child", node.id}};
    assert(has_code(validate_lockfile(invalid), ValidationCode::duplicate_dependency_alias));
    invalid = lock; invalid.nodes.push_back({RegistryLockNode{"sha256:" + std::string(64, 'd'), {"vix", "example", "child"}, {1, 0, 0, {}}, "sha256:" + std::string(64, 'e'), {{"back", "sha256:" + std::string(64, 'b')}}}});
    std::get<RegistryLockNode>(invalid.nodes.front()).dependencies.push_back({"child", "sha256:" + std::string(64, 'd')});
    assert(has_code(validate_lockfile(invalid), ValidationCode::lock_cycle));
    invalid = lock; invalid.roots.clear();
    assert(has_code(validate_lockfile(invalid), ValidationCode::noncanonical_empty_lock));
    invalid = lock; invalid.rootRequirementsDigest = "SHA256:bad";
    assert(has_code(validate_lockfile(invalid), ValidationCode::malformed_digest));
    invalid = lock; std::get<RegistryLockNode>(invalid.nodes.front()).contentDigest = "sha256:ABC";
    assert(has_code(validate_lockfile(invalid), ValidationCode::malformed_digest));
  }
}

int main()
{
  native_manifest_cases();
  package_contract_cases();
  lock_cases();
}
