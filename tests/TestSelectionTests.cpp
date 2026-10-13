#include <vix/cli/project/TestSelection.hpp>

#include <cassert>

namespace project = vix::cli::project;

namespace
{
  project::ProjectSnapshot snapshot_with_tests()
  {
    project::ProjectSnapshot snapshot;
    snapshot.project.tests = {
        {project::TestId{"ctest:0"}, "alpha", std::nullopt},
        {project::TestId{"ctest:1"}, "duplicate", std::nullopt},
        {project::TestId{"ctest:2"}, "duplicate", std::nullopt},
        {project::TestId{"ctest:3"}, "network.connect[ipv6]+timeout", std::nullopt}};
    return snapshot;
  }

  void test_exact_name_and_id_selection()
  {
    const auto snapshot = snapshot_with_tests();
    const auto alpha = project::select_test_by_name(snapshot, "alpha");
    assert(alpha.success());
    assert(alpha.test->id.value == "ctest:0");

    const auto byId = project::find_test_by_id(snapshot, project::TestId{"ctest:3"});
    assert(byId.success());
    assert(byId.test->name == "network.connect[ipv6]+timeout");
  }

  void test_missing_and_ambiguous_names()
  {
    const auto snapshot = snapshot_with_tests();
    assert(project::select_test_by_name(snapshot, "missing").error ==
           project::ProjectError::test_not_found);
    assert(project::select_test_by_name(snapshot, "duplicate").error ==
           project::ProjectError::ambiguous_test);
  }

  void test_empty_suite()
  {
    const project::ProjectSnapshot snapshot;
    assert(project::select_test_by_name(snapshot, "alpha").error ==
           project::ProjectError::test_not_found);
  }
} // namespace

int main()
{
  test_exact_name_and_id_selection();
  test_missing_and_ambiguous_names();
  test_empty_suite();
}
