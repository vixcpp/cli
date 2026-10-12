/**
 * @file CTestDiscovery.hpp
 * @brief Adapter-private CTest JSON discovery for configured CMake projects.
 */
#ifndef VIX_CLI_PROJECT_CMAKE_CTEST_DISCOVERY_HPP
#define VIX_CLI_PROJECT_CMAKE_CTEST_DISCOVERY_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/project/ProjectError.hpp>
#include <vix/cli/project/ProjectSnapshot.hpp>

namespace vix::cli::project::cmake
{
  struct SnapshotRequest;

  struct CTestDiscoveryResult
  {
    std::optional<std::vector<Test>> tests;
    std::optional<ProjectError> error;

    [[nodiscard]] bool success() const noexcept { return tests.has_value(); }
  };

  /** Builds the official, non-executing CTest JSON discovery argv. */
  [[nodiscard]] std::vector<std::string> ctest_discovery_argv(
      const Path &buildRoot,
      const std::optional<std::string> &configuration);

  /** Parses CTest's ctestInfo major-version-one JSON output. */
  [[nodiscard]] CTestDiscoveryResult parse_ctest_discovery_json(
      std::string_view document);

  /** Invokes CTest once to obtain configured test registrations. */
  [[nodiscard]] CTestDiscoveryResult discover_ctest_tests(
      const SnapshotRequest &request);
} // namespace vix::cli::project::cmake

#endif // VIX_CLI_PROJECT_CMAKE_CTEST_DISCOVERY_HPP
