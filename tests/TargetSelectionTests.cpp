#include <vix/cli/project/TargetSelection.hpp>

#include <cassert>

namespace project = vix::cli::project;

namespace
{
  project::Target target(
      const char *id,
      const char *name,
      project::TargetKind kind,
      bool buildable,
      bool runnable)
  {
    return {{id}, name, kind, {buildable, runnable}};
  }

  project::ProjectSnapshot snapshot_with(std::vector<project::Target> targets)
  {
    project::ProjectSnapshot snapshot;
    snapshot.project.targets = std::move(targets);
    return snapshot;
  }

  void test_exact_name_and_id_selection()
  {
    const auto snapshot = snapshot_with({
        target("server-id", "server", project::TargetKind::executable, true, true),
        target("unknown-id", "future", project::TargetKind::unknown, true, false)});

    const auto selected = project::select_target_by_name(snapshot, "server");
    assert(selected.success() && selected.target->id.value == "server-id");
    assert(project::select_target_by_name(snapshot, "Server").error == project::ProjectError::target_not_found);
    assert(project::select_target_by_name(snapshot, "missing").error == project::ProjectError::target_not_found);

    const auto byId = project::find_target_by_id(snapshot, {"unknown-id"});
    assert(byId.success() && byId.target->kind == project::TargetKind::unknown);
    assert(project::find_target_by_id(snapshot, {"future"}).error == project::ProjectError::target_not_found);
  }

  void test_duplicate_names_are_not_selected()
  {
    const auto snapshot = snapshot_with({
        target("one", "server", project::TargetKind::executable, true, true),
        target("two", "server", project::TargetKind::executable, true, true)});
    assert(project::select_target_by_name(snapshot, "server").error == project::ProjectError::ambiguous_target);
  }

  void test_capability_selection()
  {
    const auto snapshot = snapshot_with({
        target("run", "run", project::TargetKind::executable, true, true),
        target("headers", "headers", project::TargetKind::interface_library, false, false),
        target("core", "core", project::TargetKind::static_library, true, false)});
    assert(project::select_buildable_target_by_name(snapshot, "run").success());
    assert(project::select_buildable_target_by_name(snapshot, "headers").error == project::ProjectError::target_not_buildable);
    assert(project::select_runnable_target_by_name(snapshot, "run").success());
    assert(project::select_runnable_target_by_name(snapshot, "core").error == project::ProjectError::target_not_runnable);
  }

  void test_default_runnable_selection()
  {
    assert(project::select_default_runnable_target(snapshot_with({
        target("core", "core", project::TargetKind::static_library, true, false)})).error ==
        project::ProjectError::target_not_runnable);

    const auto one = project::select_default_runnable_target(snapshot_with({
        target("app", "app", project::TargetKind::executable, true, true)}));
    assert(one.success() && one.target->id.value == "app");

    assert(project::select_default_runnable_target(snapshot_with({
        target("client", "client", project::TargetKind::executable, true, true),
        target("server", "server", project::TargetKind::executable, true, true)})).error ==
        project::ProjectError::ambiguous_target);
  }

  void test_deterministic_listing_and_artifacts()
  {
    auto first = snapshot_with({
        target("z", "server", project::TargetKind::executable, true, true),
        target("a", "client", project::TargetKind::executable, true, true),
        target("b", "server", project::TargetKind::unknown, true, false)});
    auto second = snapshot_with({first.project.targets[2], first.project.targets[0], first.project.targets[1]});
    first.artifacts = {{{"z"}, "/tmp/one"}, {{"z"}, "/tmp/two"}};

    const auto firstList = project::list_targets(first);
    const auto secondList = project::list_targets(second);
    assert(firstList.size() == secondList.size());
    for (std::size_t i = 0; i < firstList.size(); ++i)
      assert(firstList[i].name == secondList[i].name && firstList[i].id == secondList[i].id);
    assert(firstList[0].name == "client");
    assert(firstList[1].id.value == "b");
    assert(project::list_targets(first, project::TargetFilter::runnable).size() == 2);
    assert(project::artifacts_for_target(first, {"z"}).size() == 2);
  }
} // namespace

int main()
{
  test_exact_name_and_id_selection();
  test_duplicate_names_are_not_selected();
  test_capability_selection();
  test_default_runnable_selection();
  test_deterministic_listing_and_artifacts();
}
