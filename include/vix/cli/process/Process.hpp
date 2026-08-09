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

#include <vix/engine/BuildTools.hpp>
#include <vix/engine/ExecutionPlan.hpp>
#include <vix/engine/Preset.hpp>
#include <vix/engine/SanitizerMode.hpp>

namespace vix::cli::process
{
  namespace fs = std::filesystem;

  using LinkerMode = vix::engine::LinkerMode;
  using LauncherMode = vix::engine::LauncherMode;
  using SanitizerMode = vix::engine::SanitizerMode;

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

    /** Enables structured internal Vix build diagnostics. */
    bool debug = false;

    /** Optional focused internal diagnostic scope. */
    std::string debugLogScope;

    /** Optional captured log scope to display instead of building. */
    std::string logScope;
    bool showLog = false;
    std::string logPath;

    /**
     * @brief Explains why Vix rebuilds files or targets.
     */
    bool explain = false;

    /**
     * @brief Keep the build process alive and rebuild after project changes.
     */
    bool watch = false;

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

    /** Graph executor policy: auto, on, or off. */
    std::string graphExecutor = "auto";
    bool graphExecutorExplicit = false;

    /** Lists detected target toolchains and exits. */
    bool listTargets = false;

    /** Explicit heartbeat control; unset keeps the legacy/default behavior. */
    std::optional<bool> heartbeat;

    /**
     * @brief Builds only a specific CMake target when provided.
     */
    std::string buildTarget;

    /**
     * @brief Extra arguments forwarded directly to CMake.
     */
    std::vector<std::string> cmakeArgs;

    /**
     * @brief Sanitizer instrumentation selected for this build.
     *
     * The default build remains uninstrumented.
     */
    SanitizerMode sanitizerMode = SanitizerMode::None;

    /**
     * @brief True when the sanitizer mode was explicitly selected.
     */
    bool sanitizerSelectionExplicit = false;

    bool withSqlite = false;
    bool withMySql = false;

    bool exportBin = false;
    std::string outPath;

    bool singleCpp = false;
    fs::path cppFile;

    bool warnings{false};
    std::size_t warningsPage{1};
    std::size_t warningsLimit{10};
    bool warningsPageSet{false};
    bool warningsLimitSet{false};

    bool warningCheck{false};

    bool managedSdk{false};

    /**
     * @brief Submit a Softadastra Cloud build report after the build completes.
     */
    bool report{false};
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

  using Preset = vix::engine::Preset;
  using Plan = vix::engine::ExecutionPlan;

  /**
   * @brief Normalizes a raw process exit status into a standard exit code.
   *
   * @param raw Raw process status value returned by the OS.
   * @return Normalized integer exit code.
   */
  [[nodiscard]] int normalize_exit_code(int raw) noexcept;

} // namespace vix::cli::process

#endif
