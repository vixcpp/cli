/**
 * @file TestSelection.hpp
 * @brief Exact, snapshot-scoped selection over normalized test registrations.
 */
#ifndef VIX_CLI_PROJECT_TEST_SELECTION_HPP
#define VIX_CLI_PROJECT_TEST_SELECTION_HPP

#include <optional>
#include <string_view>

#include <vix/cli/project/ProjectError.hpp>
#include <vix/cli/project/ProjectSnapshot.hpp>

namespace vix::cli::project
{
  struct TestSelectionResult
  {
    std::optional<Test> test;
    std::optional<ProjectError> error;

    [[nodiscard]] bool success() const noexcept { return test.has_value(); }
  };

  /** Resolves an opaque adapter identity, never a display name. */
  [[nodiscard]] TestSelectionResult find_test_by_id(
      const ProjectSnapshot &snapshot,
      const TestId &id);

  /** Resolves one exact user-facing CTest registration name. */
  [[nodiscard]] TestSelectionResult select_test_by_name(
      const ProjectSnapshot &snapshot,
      std::string_view name);
} // namespace vix::cli::project

#endif // VIX_CLI_PROJECT_TEST_SELECTION_HPP
