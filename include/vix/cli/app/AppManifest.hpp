/**
 *
 *  @file AppManifest.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Simple application manifest model for generated CMake builds.
 *
 */

#ifndef VIX_CLI_APP_APP_MANIFEST_HPP
#define VIX_CLI_APP_APP_MANIFEST_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vix::cli::app
{
  namespace fs = std::filesystem;

  /**
   * @brief Describes the type of C++ target declared by vix.app.
   */
  enum class AppTargetType
  {
    Executable,
    StaticLibrary,
    SharedLibrary
  };

  /**
   * @brief Converts a target type to a stable text value.
   *
   * @param type Target type.
   * @return Stable target type string.
   */
  std::string to_string(AppTargetType type);

  /**
   * @brief Parses a target type from a text value.
   *
   * Supported values:
   * - executable
   * - static
   * - static-library
   * - shared
   * - shared-library
   * - library
   *
   * @param value Raw manifest value.
   * @return Parsed target type when valid.
   */
  std::optional<AppTargetType> app_target_type_from_string(
      const std::string &value);

  /**
   * @brief Simple C++ application manifest.
   *
   * This structure represents the content of a vix.app file.
   * It is intentionally small and exists only as a user-friendly
   * layer above the internal generated CMake project.
   */
  struct AppManifest
  {
    /**
     * @brief Application or target name.
     */
    std::string name;

    /**
     * @brief Target type.
     */
    AppTargetType type{AppTargetType::Executable};

    /**
     * @brief C++ standard value.
     *
     * Example values:
     * - c++17
     * - c++20
     * - c++23
     */
    std::string standard{"c++20"};

    /**
     * @brief Source files relative to the project directory.
     */
    std::vector<std::string> sources;

    /**
     * @brief Include directories relative to the project directory.
     */
    std::vector<std::string> includeDirs;

    /**
     * @brief Preprocessor definitions.
     */
    std::vector<std::string> defines;

    /**
     * @brief CMake targets or libraries to link.
     */
    std::vector<std::string> links;

    /**
     * @brief Returns true when the manifest has the minimum valid fields.
     *
     * @return true if the manifest can be used.
     */
    bool valid() const;
  };

  /**
   * @brief Result returned when loading a vix.app file.
   */
  struct AppManifestLoadResult
  {
    /**
     * @brief Parsed manifest.
     */
    AppManifest manifest;

    /**
     * @brief Error message when loading failed.
     */
    std::string error;

    /**
     * @brief Returns true when the load operation succeeded.
     *
     * @return true if the manifest was loaded successfully.
     */
    bool success() const;
  };

  /**
   * @brief Loads a vix.app file from disk.
   *
   * @param path Path to the vix.app file.
   * @return Load result containing either a manifest or an error.
   */
  AppManifestLoadResult load_app_manifest(const fs::path &path);

} // namespace vix::cli::app

#endif // VIX_CLI_APP_APP_MANIFEST_HPP
