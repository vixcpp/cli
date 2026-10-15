#include <vix/cli/project/ProjectDiscovery.hpp>
#include <vix/cli/project/ProjectSnapshot.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <type_traits>

namespace fs = std::filesystem;
namespace project = vix::cli::project;

namespace
{
  struct TemporaryDirectory
  {
    fs::path path;

    TemporaryDirectory()
    {
      const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      path = fs::temp_directory_path() / ("vix-project-discovery-" + std::to_string(stamp));
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

  void touch(const fs::path &path)
  {
    std::ofstream file(path);
    assert(file.good());
  }

  void test_normalized_model()
  {
    static_assert(!std::is_same_v<project::TargetId, project::TestId>);

    const project::TargetId first{"adapter:first"};
    const project::TargetId second{"adapter:second"};
    const project::TestId testId{"ctest:unit"};
    assert(first.valid());
    assert(testId.valid());
    assert(!project::TargetId{}.valid());
    assert(!project::TestId{}.valid());

    project::Project model;
    model.targets = {
        {first, "duplicate", project::TargetKind::unknown, {true, false}},
        {second, "duplicate", project::TargetKind::executable, {true, true}}};
    assert(model.targets[0].name == model.targets[1].name);
    assert(model.targets[0].id != model.targets[1].id);
    assert(model.targets[0].kind == project::TargetKind::unknown);

    model.tests.push_back({testId, "unit", std::nullopt});
    assert(!model.tests.front().target.has_value());

    project::ProjectSnapshot snapshot;
    snapshot.project = model;
    snapshot.artifacts = {{second, "/tmp/app"}, {second, "/tmp/app.debug"}};
    assert(snapshot.artifacts.size() == 2);
    assert(snapshot.artifacts[0].target == snapshot.artifacts[1].target);
  }

  void test_discovery()
  {
    TemporaryDirectory temporary;
    const fs::path root = temporary.path;

    const fs::path cmake = root / "cmake";
    fs::create_directories(cmake / "a" / "b");
    touch(cmake / "CMakeLists.txt");
    const auto cmakeResult = project::discover_project(cmake / "a" / "b");
    assert(cmakeResult.success());
    assert(cmakeResult.location->root == cmake);
    assert(cmakeResult.location->system == project::ProjectSystem::cmake);

    const fs::path native = root / "native";
    fs::create_directories(native / "src");
    touch(native / "vix.toml");
    const auto nativeResult = project::discover_project(native / "src");
    assert(nativeResult.success());
    assert(nativeResult.location->root == native);
    assert(nativeResult.location->system == project::ProjectSystem::native_vix);

    const fs::path ambiguous = root / "ambiguous";
    fs::create_directories(ambiguous);
    touch(ambiguous / "vix.toml");
    touch(ambiguous / "CMakeLists.txt");
    const auto ambiguousResult = project::discover_project(ambiguous);
    assert(!ambiguousResult.success());
    assert(ambiguousResult.error == project::ProjectError::ambiguous_project_authority);

    const fs::path outer = root / "outer";
    const fs::path inner = outer / "inner";
    fs::create_directories(inner / "src" / "deep");
    touch(outer / "CMakeLists.txt");
    touch(inner / "CMakeLists.txt");
    const auto nearest = project::discover_project(inner / "src" / "deep");
    assert(nearest.success());
    assert(nearest.location->root == inner);
    assert(nearest.location->system == project::ProjectSystem::cmake);

    const fs::path obsoleteMarker = root / "obsolete-marker";
    fs::create_directories(obsoleteMarker / "nested");
    touch(obsoleteMarker / "vix.app");
    const auto ignoredLegacyMarker = project::discover_project(obsoleteMarker / "nested");
    assert(!ignoredLegacyMarker.success());
    assert(ignoredLegacyMarker.error == project::ProjectError::project_not_found);

    const fs::path source = inner / "src" / "main.cpp";
    touch(source);
    const auto fileResult = project::discover_project(source);
    assert(fileResult.success());
    assert(fileResult.location->root == inner);

    const fs::path none = root / "no-project" / "nested";
    fs::create_directories(none);
    const auto absent = project::discover_project(none);
    assert(!absent.success());
    assert(absent.error == project::ProjectError::project_not_found);

    const auto rootResult = project::discover_project(fs::path("/"));
    assert(rootResult.success() ||
           rootResult.error == project::ProjectError::project_not_found);
  }
} // namespace

int main()
{
  test_normalized_model();
  test_discovery();
}
