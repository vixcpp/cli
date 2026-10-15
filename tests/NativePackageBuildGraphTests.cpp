#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/PackageBuildGraph.hpp>
#include <vix/cli/project/native/ProjectLockState.hpp>
#include <vix/cli/project/native/TomlParser.hpp>
#include <vix/cli/project/native/Validation.hpp>

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#ifndef VIX_PACKAGE_BUILD_CMAKE_COMMAND
#define VIX_PACKAGE_BUILD_CMAKE_COMMAND "cmake"
#endif

using namespace vix::cli::project::native;
namespace fs = std::filesystem;

namespace
{
  struct TempRoot
  {
    fs::path path{fs::temp_directory_path() / ("vix-p10r-build-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()))};
    ~TempRoot() { std::error_code error; fs::remove_all(path, error); }
  };

  void write(const fs::path &path, std::string_view bytes)
  {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    assert(output.good());
  }

  std::string read(const fs::path &path)
  {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  }

  std::string quote(const fs::path &path) { return "\"" + path.string() + "\""; }

  Lockfile make_lock(const NativeProject &project, RegistryLockNode a, RegistryLockNode b)
  {
    Lockfile lock;
    const auto bId = b.id;
    a.dependencies.push_back({"b", bId});
    const auto rootRequirement = std::get<RegistryRequirement>(project.packages.front().source);
    const NormalizedDeclaredRegistry declared{rootRequirement.identity, rootRequirement.version};
    lock.roots.push_back({"a", declared, a.id});
    const NormalizedDeclaredRegistry directB{b.identity, ExactVersion{b.version}};
    lock.roots.push_back({"b", directB, b.id});
    lock.rootRequirementsDigest = project_requirements_digest(project);
    lock.nodes.emplace_back(std::move(a));
    lock.nodes.emplace_back(std::move(b));
    return lock;
  }

  void write_package(const fs::path &root, std::string_view name, std::string_view source,
    std::string_view header, std::string_view extraContract = {})
  {
    write(root / "vix.package.toml", std::string{
      "format = 1\n[package]\nsource = \"registry\"\nregistry = \"vix\"\npublisher = \"test\"\nname = \""} +
      std::string{name} + "\"\nversion = \"1.0.0\"\n" + std::string{extraContract} +
      "[integration]\ntype = \"native-vix\"\ninclude-roots = [\"include\"]\n" +
      "[[exports]]\nname = \"core\"\nproject-target = \"" + std::string{name} + "_core\"\n");
    write(root / "include" / (std::string{name} + ".hpp"), header);
    write(root / "src" / (std::string{name} + ".cpp"), source);
    write(root / "vix.toml", std::string{
      "format = 1\n[project]\nname = \""} + std::string{name} + "\"\n[[targets]]\nname = \"" +
      std::string{name} + "_core\"\nkind = \"static\"\nsources = [\"src/" +
      std::string{name} + ".cpp\"]\n[targets.includes]\npublic = [\"include\"]\n");
  }

  void integration_and_failure_tests()
  {
    TempRoot temp;
    const auto projectRoot = temp.path / "app";
    const auto packageA = temp.path / "packages" / "a";
    const auto packageB = temp.path / "packages" / "b";
    write(projectRoot / "main.cpp", "#include <a.hpp>\nint main() { return a_answer() == 43 ? 0 : 1; }\n");
    write_package(packageB, "b", "#include <b.hpp>\nint b_answer() { return 42; }\n",
      "#pragma once\nint b_answer();\n");
    write_package(packageA, "a", "#include <a.hpp>\nint a_answer() { return b_answer() + 1; }\n",
      "#pragma once\n#include <b.hpp>\nint a_answer();\n",
      "[[packages]]\nalias = \"b\"\nsource = \"registry\"\nregistry = \"vix\"\npublisher = \"test\"\nname = \"b\"\nrequirement = { exact = \"1.0.0\" }\n");

    write(packageA / "vix.toml", "format = 1\n[project]\nname = \"a\"\n[[packages]]\nalias = \"b\"\nsource = \"registry\"\nregistry = \"vix\"\npublisher = \"test\"\nname = \"b\"\nrequirement = { exact = \"1.0.0\" }\n[[targets]]\nname = \"a_core\"\nkind = \"static\"\nsources = [\"src/a.cpp\"]\n[targets.includes]\npublic = [\"include\"]\n[[targets.package-uses]]\npackage = \"b\"\ntarget = \"core\"\nvisibility = \"public\"\n");
    write(packageA / "src/a.cpp", "#include <a.hpp>\nint a_answer() { return b_answer() + 1; }\n");

    const auto appManifest = parse_native_manifest_text(
      "format = 1\n[project]\nname = \"consumer\"\n[[packages]]\nalias = \"a\"\nsource = \"registry\"\nregistry = \"vix\"\npublisher = \"test\"\nname = \"a\"\nrequirement = { exact = \"1.0.0\" }\n[[packages]]\nalias = \"b\"\nsource = \"registry\"\nregistry = \"vix\"\npublisher = \"test\"\nname = \"b\"\nrequirement = { exact = \"1.0.0\" }\n[[targets]]\nname = \"app\"\nkind = \"executable\"\nsources = [\"main.cpp\"]\n[[targets.package-uses]]\npackage = \"a\"\ntarget = \"core\"\nvisibility = \"private\"\n[[targets.package-uses]]\npackage = \"b\"\ntarget = \"core\"\nvisibility = \"private\"\n");
    assert(appManifest.valid());
    const RegistryPackageIdentity identityA{"vix", "test", "a"};
    const RegistryPackageIdentity identityB{"vix", "test", "b"};
    const Version version{1, 0, 0, std::nullopt};
    RegistryLockNode a; a.identity = identityA; a.version = version;
    a.id = registry_node_id({identityA, version});
    a.contentDigest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    RegistryLockNode b; b.identity = identityB; b.version = version;
    b.id = registry_node_id({identityB, version});
    b.contentDigest = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    auto lock = make_lock(*appManifest.value, a, b);

    const std::vector<MaterializedLockedPackage> materialized{
      {a.id, *PackageContentId::parse(a.contentDigest), packageA},
      {b.id, *PackageContentId::parse(b.contentDigest), packageB}};
    const auto graph = generate_package_build_graph(*appManifest.value, projectRoot, lock, materialized);
    assert(graph.valid() && graph.cmakeLists);
    assert(graph.cmakeLists->find("set(CMAKE_EXPORT_COMPILE_COMMANDS ON)") != std::string::npos);
    assert(graph.cmakeLists->find("add_library(a_core STATIC") != std::string::npos);
    assert(graph.cmakeLists->find("target_include_directories(b_core") != std::string::npos);
    assert(graph.cmakeLists->find("target_link_libraries(app PRIVATE a_core)") != std::string::npos);
    assert(graph.cmakeLists->find("target_link_libraries(app PRIVATE b_core)") != std::string::npos);
    assert(graph.cmakeLists->find("target_link_libraries(a_core PUBLIC b_core)") != std::string::npos);
    auto registeredTestProject = *appManifest.value;
    registeredTestProject.tests.push_back({"app-smoke", "app"});
    const auto testGraph = generate_package_build_graph(registeredTestProject, projectRoot, lock, materialized);
    assert(testGraph.valid() && testGraph.cmakeLists);
    assert(testGraph.cmakeLists->find("enable_testing()") != std::string::npos);
    assert(testGraph.cmakeLists->find("add_test(NAME [[app-smoke]] COMMAND $<TARGET_FILE:app>)") != std::string::npos);
    const auto repeated = generate_package_build_graph(*appManifest.value, projectRoot, lock, materialized);
    assert(repeated.valid() && repeated.cmakeLists == graph.cmakeLists);

    auto missing = generate_package_build_graph(*appManifest.value, projectRoot, lock,
      {{a.id, *PackageContentId::parse(a.contentDigest), packageA}});
    assert(!missing.valid() && missing.issues.front().code == PackageBuildGraphIssueCode::missing_materialization);
    const auto emptyPackageRoot = temp.path / "empty-package";
    fs::create_directories(emptyPackageRoot);
    auto missingContract = generate_package_build_graph(*appManifest.value, projectRoot, lock,
      {{a.id, *PackageContentId::parse(a.contentDigest), packageA},
       {b.id, *PackageContentId::parse(b.contentDigest), emptyPackageRoot}});
    assert(!missingContract.valid() && missingContract.issues.front().code == PackageBuildGraphIssueCode::missing_package_metadata);
    auto badUseProject = *appManifest.value;
    badUseProject.targets.front().packageUses.front().exportName = "absent";
    auto missingExport = generate_package_build_graph(badUseProject, projectRoot, lock, materialized);
    assert(!missingExport.valid());
    auto collidingProject = *appManifest.value;
    collidingProject.targets.front().name = "a_core";
    const auto collision = generate_package_build_graph(collidingProject, projectRoot, lock, materialized);
    assert(!collision.valid() && collision.issues.front().code == PackageBuildGraphIssueCode::target_collision);

    write(packageB / "vix.package.toml", "format = 1\n[package]\nsource = \"registry\"\nregistry = \"vix\"\npublisher = \"test\"\nname = \"b\"\nversion = \"1.0.0\"\n[integration]\ntype = \"cmake-config\"\nconfig-name = \"BConfig\"\n[[exports]]\nname = \"core\"\ncmake-target = \"B::b\"\n");
    const auto cmakeConfigGraph = generate_package_build_graph(*appManifest.value, projectRoot, lock, materialized);
    assert(cmakeConfigGraph.valid() && cmakeConfigGraph.cmakeLists);
    assert(cmakeConfigGraph.cmakeLists->find("find_package(BConfig CONFIG REQUIRED)") != std::string::npos);
    assert(cmakeConfigGraph.cmakeLists->find("if(NOT TARGET B::b)") != std::string::npos);
    write(packageB / "vix.package.toml", "format = 2\n");
    const auto invalidContract = generate_package_build_graph(*appManifest.value, projectRoot, lock, materialized);
    assert(!invalidContract.valid() && invalidContract.issues.front().code == PackageBuildGraphIssueCode::invalid_package_contract);

    // Exercise the emitted graph with a real CMake configure/build/run during
    // the human focused test. This test is never run by Codex in static mode.
    const auto generatedSource = temp.path / "generated";
    const auto buildRoot = temp.path / "build";
    write(generatedSource / "CMakeLists.txt", *graph.cmakeLists +
      "file(GENERATE OUTPUT \"${CMAKE_BINARY_DIR}/vix-test-app-path.txt\" CONTENT \"$<TARGET_FILE:app>\")\n");
    const auto configure = std::string{"\""} + VIX_PACKAGE_BUILD_CMAKE_COMMAND + "\" -S " +
      quote(generatedSource) + " -B " + quote(buildRoot);
    assert(std::system(configure.c_str()) == 0);
    const auto build = std::string{"\""} + VIX_PACKAGE_BUILD_CMAKE_COMMAND + "\" --build " + quote(buildRoot);
    assert(std::system(build.c_str()) == 0);
    const auto compileCommands = read(buildRoot / "compile_commands.json");
    assert(compileCommands.find((packageB / "include").string()) != std::string::npos);
    assert(compileCommands.find((packageA / "include").string()) != std::string::npos);
    const auto executable = fs::path{read(buildRoot / "vix-test-app-path.txt")};
    assert(!executable.empty() && std::system(quote(executable).c_str()) == 0);
  }
}

int main()
{
  integration_and_failure_tests();
}
