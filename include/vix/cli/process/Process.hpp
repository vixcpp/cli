/**
 *
 *  @file Process.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_PROCESS_HPP
#define VIX_CLI_PROCESS_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vix::cli::process
{
  namespace fs = std::filesystem;

  /**
   * @brief Selects which linker strategy Vix should use for builds.
   */
  enum class LinkerMode
  {
    Auto,
    Default,
    Mold,
    Lld
  };

  /**
   * @brief Selects which compiler launcher/cache Vix should use.
   */
  enum class LauncherMode
  {
    Auto,
    None,
    Sccache,
    Ccache
  };

  /**
   * @brief Parsed options for the `vix build` command.
   *
   * This structure stores all user-facing build configuration flags
   * after command-line parsing.
   */
  struct Options
  {
    /**
     * @brief Selected embedded preset.
     *
     * Supported values include:
     * - dev
     * - dev-ninja
     * - release
     */
    std::string preset = "dev-ninja";

    /**
     * @brief Cross-compilation target triple passed with `--target`.
     */
    std::string targetTriple;

    /**
     * @brief Optional sysroot path for cross-compilation.
     */
    std::string sysroot;

    /**
     * @brief Enables static linking when supported by the project.
     */
    bool linkStatic = false;

    /**
     * @brief Number of parallel jobs to use for the build.
     *
     * A value of 0 means "auto-detect".
     */
    int jobs = 0;

    /**
     * @brief Forces a fresh configure step.
     */
    bool clean = false;

    /**
     * @brief Enables quiet console output.
     */
    bool quiet = false;

    /**
     * @brief Enables detailed build output.
     *
     * When false, `vix build` should prefer a minimal product-style output.
     */
    bool verbose = false;

    /**
     * @brief Optional project directory passed with `--dir`.
     */
    std::string dir;

    /**
     * @brief Enables fast no-op detection before building.
     */
    bool fast = false;

    /**
     * @brief Enables signature/configuration cache reuse.
     */
    bool useCache = true;

    /**
     * @brief Preferred linker mode.
     */
    LinkerMode linker = LinkerMode::Auto;

    /**
     * @brief Preferred compiler launcher mode.
     */
    LauncherMode launcher = LauncherMode::Auto;

    /**
     * @brief Enables Ninja progress status output.
     */
    bool status = true;

    /**
     * @brief Enables Ninja dry-run up-to-date detection.
     */
    bool dryUpToDate = true;

    /**
     * @brief Enables raw CMake verbose configure output.
     */
    bool cmakeVerbose = false;

    /**
     * @brief Builds only a specific CMake target when provided.
     */
    std::string buildTarget;

    /**
     * @brief Extra arguments forwarded directly to CMake.
     */
    std::vector<std::string> cmakeArgs;
  };

  /**
   * @brief Result of a spawned child process.
   */
  struct ExecResult
  {
    /**
     * @brief Normalized process exit code.
     */
    int exitCode = 0;

    /**
     * @brief User-readable reconstructed command line.
     */
    std::string displayCommand;

    /**
     * @brief True if the process produced any stdout/stderr output.
     */
    bool producedOutput = false;

    /**
     * @brief Captured first output line when available.
     */
    std::string capturedFirstLine;
  };

  /**
   * @brief Embedded build preset description.
   */
  struct Preset
  {
    /**
     * @brief Preset public name.
     */
    std::string name;

    /**
     * @brief CMake generator name, usually "Ninja".
     */
    std::string generator;

    /**
     * @brief CMake build type, such as "Debug" or "Release".
     */
    std::string buildType;

    /**
     * @brief Build directory name associated with the preset.
     */
    std::string buildDirName;
  };

  /**
   * @brief Fully resolved execution plan for a build.
   *
   * This contains all derived paths, resolved tools, generated files,
   * and CMake variables needed to configure and build the project.
   */
  struct Plan
  {
    /**
     * @brief Root project directory containing the main CMakeLists.txt.
     */
    fs::path projectDir;

    /**
     * @brief Resolved embedded preset.
     */
    Preset preset;

    /**
     * @brief Build directory used for configure/build artifacts.
     */
    fs::path buildDir;

    /**
     * @brief Path to the configure log file.
     */
    fs::path configureLog;

    /**
     * @brief Path to the build log file.
     */
    fs::path buildLog;

    /**
     * @brief Path to the configuration signature file.
     */
    fs::path sigFile;

    /**
     * @brief Path to the generated toolchain file when cross-compiling.
     */
    fs::path toolchainFile;

    /**
     * @brief Resolved CMake cache variables passed during configure.
     */
    std::vector<std::pair<std::string, std::string>> cmakeVars;

    /**
     * @brief Signature used to detect whether reconfigure is needed.
     */
    std::string signature;

    /**
     * @brief Resolved compiler launcher executable, if any.
     */
    std::optional<std::string> launcher;

    /**
     * @brief Resolved fast-linker compiler flag, if any.
     */
    std::optional<std::string> fastLinkerFlag;

    /**
     * @brief Fingerprint of important project files used for caching.
     */
    std::string projectFingerprint;
  };

  /**
   * @brief Normalizes a raw process exit status into a standard exit code.
   *
   * @param raw Raw process status value returned by the OS.
   * @return Normalized integer exit code.
   */
  [[nodiscard]] int normalize_exit_code(int raw) noexcept;

} // namespace vix::cli::process

#endif
