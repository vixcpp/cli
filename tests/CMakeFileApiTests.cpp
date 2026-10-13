#include <vix/cli/project/cmake/CMakeFileApi.hpp>
#include <vix/cli/project/cmake/CMakeProjectAdapter.hpp>
#include <vix/cli/project/cmake/CTestDiscovery.hpp>
#include <vix/cli/project/TargetSelection.hpp>

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <type_traits>

namespace fs = std::filesystem;
namespace project = vix::cli::project;
namespace cmake_adapter = vix::cli::project::cmake;

namespace
{
  struct TemporaryDirectory
  {
    fs::path path;
    TemporaryDirectory()
    {
      const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      path = fs::temp_directory_path() / ("vix-p3-file-api-" + std::to_string(stamp));
      std::error_code error;
      fs::create_directories(path, error);
      assert(!error);
    }
    ~TemporaryDirectory()
    {
      std::error_code error;
      fs::remove_all(path, error);
    }
  };

  const project::Target &target(const project::ProjectSnapshot &snapshot, const std::string &name)
  {
    for (const auto &candidate : snapshot.project.targets)
      if (candidate.name == name)
        return candidate;
    assert(false);
    return snapshot.project.targets.front();
  }

  void test_synthetic_mapping()
  {
    assert(cmake_adapter::target_kind_from_cmake_type("MODULE_LIBRARY") == project::TargetKind::module_library);
    assert(cmake_adapter::target_kind_from_cmake_type("INTERFACE_LIBRARY") == project::TargetKind::interface_library);

    const fs::path root = "/tmp/vix-p3-source";
    const fs::path build = "/tmp/vix-p3-build";
    cmake_adapter::CMakeFileApiCodemodel model;
    model.major = 2;
    model.minor = 9;
    model.sourceRoot = root;
    model.buildRoot = build;
    model.projectName = "synthetic";
    cmake_adapter::CMakeFileApiConfiguration configuration;
    configuration.name = "";
    configuration.targets = {
        {"first", "duplicate", "", "FUTURE_TYPE", false, false, false,
         {build / "one", build / "two"}},
        {"imported", "dependency", "", "INTERFACE_LIBRARY", false, true, true, {}},
        {"generated", "helper", "", "UTILITY", true, false, false, {}},
        {"utility", "utility", "", "UTILITY", false, false, false, {}},
        {"one", "one", "one-bin", "EXECUTABLE", false, false, false,
         {build / "one-bin"}},
        {"many", "many", "main-bin", "EXECUTABLE", false, false, false,
         {build / "aux", build / "main-bin"}},
        {"none", "none", "main-bin", "EXECUTABLE", false, false, false,
         {build / "aux"}},
        {"duplicate-primary", "duplicate-primary", "main-bin", "EXECUTABLE", false, false, false,
         {build / "a" / "main-bin", build / "b" / "main-bin"}}};
    model.configurations = {configuration};
    const project::ProjectLocation location{root, project::ProjectSystem::cmake};
    const auto result = cmake_adapter::make_project_snapshot(location, {build, std::nullopt}, model);
    assert(result.success());
    assert(result.snapshot->project.targets.size() == 6);
    assert(target(*result.snapshot, "duplicate").kind == project::TargetKind::unknown);
    assert(target(*result.snapshot, "utility").capabilities.buildable);
    assert(target(*result.snapshot, "one").capabilities.runnable);
    assert(target(*result.snapshot, "many").capabilities.runnable);
    assert(!target(*result.snapshot, "none").capabilities.runnable);
    assert(!target(*result.snapshot, "duplicate-primary").capabilities.runnable);

    model.configurations.push_back({"Release", {}});
    const auto required = cmake_adapter::make_project_snapshot(location, {build, std::nullopt}, model);
    assert(required.error == project::ProjectError::configuration_required);
    const auto selected = cmake_adapter::make_project_snapshot(location, {build, std::string{"Release"}}, model);
    assert(selected.success());
    const auto missing = cmake_adapter::make_project_snapshot(location, {build, std::string{"Missing"}}, model);
    assert(missing.error == project::ProjectError::configuration_not_found);
    const auto wrong = cmake_adapter::make_project_snapshot({"/tmp/other", project::ProjectSystem::cmake}, {build, std::nullopt}, model);
    assert(wrong.error == project::ProjectError::configured_metadata_unavailable);
  }

  void test_missing_and_malformed_metadata()
  {
    TemporaryDirectory temporary;
    const auto missing = cmake_adapter::load_cmake_file_api_codemodel(temporary.path);
    assert(missing.error == project::ProjectError::configured_metadata_unavailable);

    const fs::path reply = temporary.path / ".cmake/api/v1/reply";
    fs::create_directories(reply);
    const fs::path index = reply / "index-test.json";
    std::ofstream(index) << "{";
    const auto malformed = cmake_adapter::load_cmake_file_api_codemodel(temporary.path);
    assert(malformed.error == project::ProjectError::configured_metadata_unavailable);

    std::ofstream(index) << R"({"reply":{}})";
    const auto noClient = cmake_adapter::load_cmake_file_api_codemodel(temporary.path);
    assert(noClient.error == project::ProjectError::configured_metadata_unavailable);

    std::ofstream(index) << R"({"reply":{"client-vix":{}}})";
    const auto noCodemodel = cmake_adapter::load_cmake_file_api_codemodel(temporary.path);
    assert(noCodemodel.error == project::ProjectError::configured_metadata_unavailable);

    std::ofstream(index) << R"({"reply":{"client-vix":{"codemodel-v2":{"jsonFile":"missing.json","version":{"major":2,"minor":0}}}}})";
    const auto missingReply = cmake_adapter::load_cmake_file_api_codemodel(temporary.path);
    assert(missingReply.error == project::ProjectError::configured_metadata_unavailable);

    std::ofstream(reply / "malformed-codemodel.json") << "{";
    std::ofstream(index) << R"({"reply":{"client-vix":{"codemodel-v2":{"jsonFile":"malformed-codemodel.json","version":{"major":2,"minor":0}}}}})";
    const auto malformedReply = cmake_adapter::load_cmake_file_api_codemodel(temporary.path);
    assert(malformedReply.error == project::ProjectError::configured_metadata_unavailable);
  }

  void test_real_file_api_fixture()
  {
    TemporaryDirectory temporary;
    const fs::path source = VIX_P3_FIXTURE_SOURCE;
    const fs::path build = temporary.path / "build";
    assert(cmake_adapter::prepare_cmake_file_api_query(build).success());
    const fs::path query = build / ".cmake/api/v1/query/client-vix/codemodel-v2";
    assert(fs::exists(query));
    assert(fs::file_size(query) == 0);

    const std::string command = std::string{"\""} + VIX_P3_CMAKE_COMMAND + "\" -S \"" +
        source.string() + "\" -B \"" + build.string() + "\" -G Ninja";
    assert(std::system(command.c_str()) == 0);

    const project::ProjectLocation location{source, project::ProjectSystem::cmake};
    const auto loaded = cmake_adapter::load_cmake_project_snapshot(location, {build, std::nullopt});
    assert(loaded.success());
    const auto &snapshot = *loaded.snapshot;
    assert(snapshot.project.name == "vix_p3_fixture");
    assert(snapshot.project.root == fs::absolute(source).lexically_normal());
    assert(snapshot.context.buildRoot == fs::absolute(build).lexically_normal());
    assert(target(snapshot, "core").kind == project::TargetKind::static_library);
    assert(target(snapshot, "shared").kind == project::TargetKind::shared_library);
    assert(target(snapshot, "objects").kind == project::TargetKind::object_library);
    assert(target(snapshot, "server").kind == project::TargetKind::executable);
    assert(target(snapshot, "client").kind == project::TargetKind::executable);
    assert(target(snapshot, "generate_assets").kind == project::TargetKind::utility);
    assert(target(snapshot, "subcomponent").kind == project::TargetKind::static_library);
    assert(target(snapshot, "server").capabilities.buildable);
    assert(target(snapshot, "server").capabilities.runnable);
    assert(target(snapshot, "client").capabilities.runnable);
    assert(!target(snapshot, "core").capabilities.runnable);
    assert(target(snapshot, "server").id.valid());
    assert(target(snapshot, "server").id.value != target(snapshot, "server").name);
    assert(snapshot.project.tests.size() == 2);
    assert(snapshot.project.tests[0].name == "alpha");
    assert(snapshot.project.tests[1].name == "beta");
    assert(snapshot.project.tests[0].id != snapshot.project.tests[1].id);
    assert(!snapshot.project.tests[0].target.has_value());
    assert(!snapshot.project.tests[1].target.has_value());

    const auto server = project::select_target_by_name(snapshot, "server");
    const auto client = project::select_target_by_name(snapshot, "client");
    assert(server.success() && server.target->id == target(snapshot, "server").id);
    assert(client.success() && client.target->id == target(snapshot, "client").id);
    assert(project::select_default_runnable_target(snapshot).error == project::ProjectError::ambiguous_target);

    const auto nativeServer = cmake_adapter::resolve_native_build_target(
        location, {build, std::nullopt}, server.target->id);
    assert(nativeServer.success() && *nativeServer.target == "server");
    const auto runnableServer = cmake_adapter::resolve_runnable_artifact(
        location, {build, std::nullopt}, server.target->id);
    assert(runnableServer.success());
    assert(!fs::exists(*runnableServer.artifact));

    bool serverArtifact = false;
    bool clientArtifact = false;
    for (const auto &artifact : snapshot.artifacts)
    {
      if (artifact.target == target(snapshot, "server").id)
      {
        serverArtifact = true;
        assert(artifact.path.is_absolute());
        assert(artifact.path.lexically_relative(snapshot.context.buildRoot).empty() == false);
      }
      if (artifact.target == target(snapshot, "client").id)
        clientArtifact = true;
    }
    assert(serverArtifact && clientArtifact);

    // CMake 3.28's codemodel may not expose source-less interface targets.
    for (const auto &candidate : snapshot.project.targets)
      if (candidate.name == "headers")
        assert(candidate.kind == project::TargetKind::interface_library);
  }
} // namespace

int main()
{
  static_assert(!std::is_same_v<project::TargetId, project::TestId>);
  test_synthetic_mapping();
  test_missing_and_malformed_metadata();
  test_real_file_api_fixture();
}
