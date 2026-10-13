/** @file TestSelection.cpp */
#include <vix/cli/project/TestSelection.hpp>

namespace vix::cli::project
{
  TestSelectionResult find_test_by_id(
      const ProjectSnapshot &snapshot,
      const TestId &id)
  {
    for (const Test &test : snapshot.project.tests)
      if (test.id == id)
        return {test, std::nullopt};
    return {std::nullopt, ProjectError::test_not_found};
  }

  TestSelectionResult select_test_by_name(
      const ProjectSnapshot &snapshot,
      std::string_view name)
  {
    const Test *match = nullptr;
    for (const Test &test : snapshot.project.tests)
    {
      if (test.name != name)
        continue;
      if (match != nullptr)
        return {std::nullopt, ProjectError::ambiguous_test};
      match = &test;
    }
    return match == nullptr
        ? TestSelectionResult{std::nullopt, ProjectError::test_not_found}
        : TestSelectionResult{*match, std::nullopt};
  }
} // namespace vix::cli::project
