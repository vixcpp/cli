/**
 * @file CMakeFileApi.hpp
 * @brief Internal reader for CMake's configured File API metadata.
 */
#ifndef VIX_CLI_PROJECT_CMAKE_CMAKE_FILE_API_HPP
#define VIX_CLI_PROJECT_CMAKE_CMAKE_FILE_API_HPP

#include <optional>
#include <string>
#include <vector>

#include <vix/cli/project/ProjectError.hpp>
#include <vix/cli/project/Project.hpp>

namespace vix::cli::project::cmake
{
  struct CMakeFileApiResult
  {
    std::optional<ProjectError> error;
    [[nodiscard]] bool success() const noexcept { return !error.has_value(); }
  };

  struct CMakeFileApiTarget
  {
    std::string id;
    std::string name;
    std::string type;
    bool generatorProvided{false};
    bool imported{false};
    bool abstract{false};
    std::vector<Path> artifacts;
  };

  struct CMakeFileApiConfiguration
  {
    std::string name;
    std::vector<CMakeFileApiTarget> targets;
  };

  struct CMakeFileApiCodemodel
  {
    int major{0};
    int minor{0};
    Path sourceRoot;
    Path buildRoot;
    std::string projectName;
    std::vector<CMakeFileApiConfiguration> configurations;
  };

  struct CMakeFileApiCodemodelResult
  {
    std::optional<CMakeFileApiCodemodel> codemodel;
    std::optional<ProjectError> error;
    [[nodiscard]] bool success() const noexcept { return codemodel.has_value(); }
  };

  /** Creates Vix's empty, stateless codemodel-v2 query without running CMake. */
  [[nodiscard]] CMakeFileApiResult prepare_cmake_file_api_query(const Path &buildRoot);

  /** Reads the current Vix client reply and follows its JSON references. */
  [[nodiscard]] CMakeFileApiCodemodelResult load_cmake_file_api_codemodel(
      const Path &buildRoot);
} // namespace vix::cli::project::cmake

#endif // VIX_CLI_PROJECT_CMAKE_CMAKE_FILE_API_HPP
