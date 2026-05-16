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
   * @brief Package dependency declared in vix.app.
   *
   * Manifest syntax accepted by the parser:
   * - "<name>"
   * - "<name>:REQUIRED"
   * - "<name>:COMPONENTS=a,b"
   * - "<name>:COMPONENTS=a,b:REQUIRED"
   *
   * The order of `COMPONENTS=...` and `REQUIRED` does not matter.
   * Whitespace around components is trimmed.
   */
  struct AppPackage
  {
    /**
     * @brief Package name as passed to CMake `find_package`.
     */
    std::string name;

    /**
     * @brief Optional component list.
     */
    std::vector<std::string> components;

    /**
     * @brief True when the package must be marked REQUIRED.
     */
    bool required{false};
  };

  /**
   * @brief Resource entry copied into the build output directory.
   *
   * Manifest syntax accepted by the parser:
   * - "<src>"           copies `<src>` next to the built target,
   *                     preserving the basename of `<src>`
   * - "<src>=<dest>"    copies `<src>` to `<dest>` next to the
   *                     built target
   *
   * Paths are interpreted relative to the user project directory.
   */
  struct AppResource
  {
    /**
     * @brief Source path relative to the user project directory.
     */
    std::string source;

    /**
     * @brief Destination path relative to the runtime output
     * directory of the target. May be empty to keep the source
     * basename.
     */
    std::string destination;
  };

  /**
   * @brief Simple C++ application manifest.
   *
   * This structure represents the content of a vix.app file.
   * It is intentionally small and exists only as a user-friendly
   * layer above the internal generated CMake project.
   *
   * Backward compatibility:
   * Manifests using only `name`, `type`, `standard`, `sources`,
   * `include_dirs`, `defines` and `links` are still fully supported.
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
     * - c++11
     * - c++14
     * - c++17
     * - c++20
     * - c++23
     * - c++26
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
     * @brief Raw compile options forwarded to the compiler.
     */
    std::vector<std::string> compileOptions;

    /**
     * @brief Raw link options forwarded to the linker.
     */
    std::vector<std::string> linkOptions;

    /**
     * @brief CMake `target_compile_features` entries (for example
     * `cxx_std_20`, `cxx_constexpr`).
     */
    std::vector<std::string> compileFeatures;

    /**
     * @brief External packages resolved with `find_package`.
     */
    std::vector<AppPackage> packages;

    /**
     * @brief Resources copied next to the built target.
     */
    std::vector<AppResource> resources;

    /**
     * @brief Optional output directory relative to the build tree.
     *
     * When set, the runtime, library and archive output directories
     * of the generated target are pointed at this location.
     */
    std::string outputDir;

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
