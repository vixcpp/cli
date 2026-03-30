/**
 *
 *  @file GlobalPackages.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Global packages integration for vix build.
 *
 *  This module is responsible for:
 *
 *    - Loading globally installed Vix packages
 *    - Generating the CMake integration file used by `vix build`
 *    - Injecting header-only packages
 *    - Preparing compiled package reuse through cached artifact prefixes
 *    - Falling back to source-based integration when needed
 *
 */

#ifndef VIX_CLI_CMAKE_GLOBALPACKAGES_HPP
#define VIX_CLI_CMAKE_GLOBALPACKAGES_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  /**
   * @brief Description of a globally installed package
   *
   * This structure represents one package entry loaded from:
   *
   *   ~/.vix/global/installed.json
   *
   * A package can be:
   *   - header-only
   *   - source-based / compiled
   *
   * For compiled packages, Vix may also look for reusable cached
   * artifact prefixes under:
   *
   *   ~/.vix/cache/build/
   */
  struct GlobalPackage
  {
    std::string id;                    ///< Full package id (ex: @softadastra/core@1.3.0)
    std::string pkgDir;                ///< Normalized package directory key
    std::string includeDir{"include"}; ///< Include directory relative to installedPath
    std::string type{"header-only"};   ///< Package type: header-only or compiled/source
    fs::path installedPath;            ///< Installed source/package root path
  };

  /**
   * @brief Load globally installed packages from the global manifest
   *
   * @return Vector of discovered global packages
   */
  std::vector<GlobalPackage> load_global_packages();

  /**
   * @brief Generate the CMake integration file for global packages
   *
   * The generated CMake content can:
   *   - add include directories for header-only packages
   *   - inject artifact cache prefixes into CMAKE_PREFIX_PATH
   *   - fallback to add_subdirectory(...) for source packages
   *
   * @param packages List of global packages
   * @return Generated CMake script content
   */
  std::string make_global_packages_cmake(
      const std::vector<GlobalPackage> &packages);

} // namespace vix::cli::build

#endif
