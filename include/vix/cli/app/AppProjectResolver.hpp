/**
 *
 *  @file AppProjectResolver.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Project resolver for CMake and vix.app based applications.
 *
 */

#ifndef VIX_CLI_APP_APP_PROJECT_RESOLVER_HPP
#define VIX_CLI_APP_APP_PROJECT_RESOLVER_HPP

#include <filesystem>
#include <string>

namespace vix::cli::app
{
  namespace fs = std::filesystem;

  /**
   * @brief Describes which project input was selected.
   */
  enum class AppProjectKind
  {
    Unknown,
    CMake,
    VixApp
  };

  /**
   * @brief Converts a project kind to a stable string.
   *
   * @param kind Project kind.
   * @return Stable string value.
   */
  std::string to_string(AppProjectKind kind);

  /**
   * @brief Resolved project information used by build and run commands.
   */
  struct AppProjectResolveResult
  {
    /**
     * @brief Selected project kind.
     */
    AppProjectKind kind{AppProjectKind::Unknown};

    /**
     * @brief Original user project directory.
     *
     * This is where the user runs Vix from, and where build directories
     * should still be created.
     */
    fs::path userProjectDir;

    /**
     * @brief CMake source directory passed to cmake -S.
     *
     * For normal CMake projects, this is the same as userProjectDir.
     * For vix.app projects, this is the generated internal CMake directory.
     */
    fs::path cmakeSourceDir;

    /**
     * @brief Path to the active CMakeLists.txt file.
     */
    fs::path cmakeListsPath;

    /**
     * @brief Path to vix.app when the project uses vix.app.
     */
    fs::path appManifestPath;

    /**
     * @brief Target name resolved from the project.
     */
    std::string targetName;

    /**
     * @brief True when CMakeLists.txt was generated from vix.app.
     */
    bool generated{false};

    /**
     * @brief Error message when resolution failed.
     */
    std::string error;

    /**
     * @brief Returns true when project resolution succeeded.
     *
     * @return true if the result is usable.
     */
    bool success() const;
  };

  /**
   * @brief Resolves a C++ project for Vix build or run.
   *
   * Resolution order:
   * 1. CMakeLists.txt
   * 2. vix.app
   *
   * If CMakeLists.txt exists, the current build behavior is preserved.
   * vix.app is used only when no CMakeLists.txt is present.
   *
   * @param base Directory to start from.
   * @return Resolved project result.
   */
  AppProjectResolveResult resolve_app_project(const fs::path &base);

} // namespace vix::cli::app

#endif // VIX_CLI_APP_APP_PROJECT_RESOLVER_HPP
