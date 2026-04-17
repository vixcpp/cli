/**
 *
 *  @file CheckDetail.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CHECK_DETAIL_HPP
#define VIX_CHECK_DETAIL_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace vix::commands::CheckCommand::detail
{
  namespace fs = std::filesystem;

  /**
   * @brief Parsed options for `vix check`.
   *
   * This structure is shared by both project mode and script mode.
   *
   * Project mode:
   * - configure
   * - build
   * - optional tests
   * - optional runtime validation
   *
   * Script mode:
   * - temporary configure
   * - build
   * - optional runtime validation when sanitizers are enabled
   */
  struct Options
  {
    /**
     * @name Common options
     * @{
     */

    /// Explicit project directory passed with --dir.
    std::string dir;

    /// Configure preset name used in project mode.
    std::string preset = "dev-ninja";

    /// Optional build preset override.
    std::string buildPreset;

    /// Number of parallel jobs for the build step.
    int jobs = 0;

    /// Minimal output mode.
    bool quiet = false;

    /// More verbose output mode.
    bool verbose = false;

    /// Optional log level for the current check session.
    std::string logLevel;

    /** @} */

    /**
     * @name Script mode
     * @{
     */

    /// True when the user passed a single .cpp file.
    bool singleCpp = false;

    /// Absolute path of the .cpp file to validate.
    fs::path cppFile;

    /// Use local .vix-scripts instead of global ~/.vix/cache/scripts.
    bool localCache = false;

    /// Enable AddressSanitizer + UBSan.
    bool enableSanitizers = false;

    /// Enable UBSan only.
    bool enableUbsanOnly = false;

    /// Enable SQLite backend.
    bool withSqlite = false;

    /// Enable MySQL backend.
    bool withMySql = false;

    /** @} */

    /**
     * @name Project test options
     * @{
     */

    /// Run tests after build.
    bool tests = false;

    /// Optional CTest preset override.
    std::string ctestPreset;

    /// Extra arguments forwarded to CTest.
    std::vector<std::string> ctestArgs;

    /** @} */

    /**
     * @name Runtime validation
     * @{
     */

    /// Run the built executable after build.
    bool runAfterBuild = false;

    /// Runtime timeout in seconds. 0 means no explicit timeout override.
    int runTimeoutSec = 0;

    bool full = false;

    /** @} */
  };

  /**
   * @brief Parse CLI arguments for `vix check`.
   *
   * @param args Raw command arguments.
   * @return Parsed options.
   */
  Options parse(const std::vector<std::string> &args);

  /**
   * @brief Validate a single C++ script in temporary project mode.
   *
   * @param opt Parsed options.
   * @return Process exit code.
   */
  int check_single_cpp(const Options &opt);

  /**
   * @brief Validate a CMake project.
   *
   * @param opt Parsed options.
   * @param projectDir Resolved project directory.
   * @return Process exit code.
   */
  int check_project(const Options &opt, const fs::path &projectDir);

} // namespace vix::commands::CheckCommand::detail

#endif // VIX_CHECK_DETAIL_HPP
