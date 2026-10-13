/**
 * @file CTestExecution.hpp
 * @brief Adapter-private CTest execution argv construction.
 */
#ifndef VIX_CLI_PROJECT_CMAKE_CTEST_EXECUTION_HPP
#define VIX_CLI_PROJECT_CMAKE_CTEST_EXECUTION_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/project/Project.hpp>

namespace vix::cli::project::cmake
{
  /** Escapes a literal test name for CTest's regex transport. */
  [[nodiscard]] std::string ctest_exact_name_regex(std::string_view name);

  /** Builds argv for suite or exact-registration execution through CTest. */
  [[nodiscard]] std::vector<std::string> ctest_execution_argv(
      const Path &buildRoot,
      const std::optional<std::string> &configuration,
      const std::optional<std::string> &exactTestName,
      const std::vector<std::string> &nativeArguments = {});
} // namespace vix::cli::project::cmake

#endif // VIX_CLI_PROJECT_CMAKE_CTEST_EXECUTION_HPP
