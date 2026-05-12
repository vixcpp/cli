/**
 *
 *  @file AppCMakeGenerator.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  CMake generator for simple vix.app projects.
 *
 */

#ifndef VIX_CLI_APP_APP_CMAKE_GENERATOR_HPP
#define VIX_CLI_APP_APP_CMAKE_GENERATOR_HPP

#include <filesystem>
#include <string>

#include <vix/cli/app/AppManifest.hpp>

namespace vix::cli::app
{
  namespace fs = std::filesystem;

  /**
   * @brief Result returned after generating an internal CMake project.
   */
  struct AppCMakeGenerateResult
  {
    /**
     * @brief Directory containing the generated CMakeLists.txt.
     */
    fs::path sourceDir;

    /**
     * @brief Path to the generated CMakeLists.txt file.
     */
    fs::path cmakeListsPath;

    /**
     * @brief Error message when generation failed.
     */
    std::string error;

    /**
     * @brief Returns true when the generation succeeded.
     *
     * @return true if the generated CMake project is ready.
     */
    bool success() const;
  };

  /**
   * @brief Builds the generated CMakeLists.txt content for a vix.app manifest.
   *
   * @param manifest Parsed vix.app manifest.
   * @param projectDir Original user project directory.
   * @return Generated CMakeLists.txt content.
   */
  std::string generate_app_cmake_lists_content(
      const AppManifest &manifest,
      const fs::path &projectDir);

  /**
   * @brief Generates an internal CMake project for a simple vix.app project.
   *
   * The generated project is written under:
   *
   * .vix/generated/app/CMakeLists.txt
   *
   * @param manifest Parsed vix.app manifest.
   * @param projectDir Original user project directory.
   * @return Generation result.
   */
  AppCMakeGenerateResult generate_app_cmake_project(
      const AppManifest &manifest,
      const fs::path &projectDir);

} // namespace vix::cli::app

#endif // VIX_CLI_APP_APP_CMAKE_GENERATOR_HPP
