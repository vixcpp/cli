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
    std::size_t warningsPage{1};
    std::size_t warningsLimit{10};
    std::vector<std::string> cmakeArgs;
    std::vector<std::string> compilerArgs;
    std::vector<std::string> linkerArgs;

    /**
     * @brief Selected embedded preset.
     *
     * Supported values include:
     * - dev
     * - dev-ninja
     * - release
     */
    std::string preset = "dev-ninja";
    std::string targetTriple;
    std::string sysroot;
    std::string debugLogScope;
    std::string logScope;
    std::string logPath;
    std::string dir;
    std::string graphExecutor = "auto";
    std::string buildTarget;
    std::string outPath;
    fs::path cppFile;

    /**
     * @brief Number of parallel jobs to use; zero means auto-detect.
     */
    int jobs = 0;
    LinkerMode linker = LinkerMode::Auto;
    LauncherMode launcher = LauncherMode::Auto;
    SanitizerMode sanitizerMode = SanitizerMode::None;

    bool linkStatic = false;
    bool clean = false;
    bool quiet = false;
    bool verbose = false;
    bool debug = false;
    bool showLog = false;
    bool explain = false;
    bool watch = false;
    bool fast = false;
    bool useCache = true;
    bool status = true;
    bool dryUpToDate = true;
    bool cmakeVerbose = false;
    bool graphExecutorExplicit = false;
    bool listTargets = false;
    bool requireProjectMetadata{false};
    bool sanitizerSelectionExplicit = false;
    bool exportBin = false;
    bool singleCpp = false;
    bool warnings{false};
    bool warningsPageSet{false};
    bool warningsLimitSet{false};
    bool warningCheck{false};
    std::optional<bool> heartbeat;
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
