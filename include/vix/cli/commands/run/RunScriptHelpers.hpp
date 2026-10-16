/**
 *
 *  @file RunScriptHelpers.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_RUN_SCRIPT_HELPERS_HPP
#define VIX_RUN_SCRIPT_HELPERS_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/commands/DiagnosticMode.hpp>

namespace vix::commands::RunCommand::detail
{
  namespace fs = std::filesystem;
  using DiagnosticMode = ::vix::cli::commands::DiagnosticMode;

  /**
   * @brief Prints the watch-mode restart banner.
   *
   * @param path Path of the file that triggered the restart.
   * @param label Message displayed while rebuilding or restarting.
   */
  void print_watch_restart_banner(const fs::path &path, std::string_view label);

  /**
   * @brief Checks whether address/undefined sanitizer instrumentation is enabled.
   * @param mode Resolved Vix diagnostic mode.
   * @return True for address/undefined instrumentation modes (not TSan).
   */
  bool want_sanitizers(DiagnosticMode mode);

  /**
   * @brief Returns the sanitizer mode string used by generated CMake projects.
   *
   * Possible return values:
   * - "asan"
   * - "asan_ubsan"
   * - "ubsan"
   * - "tsan"
   * - "none"
   *
   * @return Sanitizer mode string.
   */
  std::string sanitizer_mode_string(DiagnosticMode mode);

  /**
   * @brief Builds the configuration signature for generated script CMake projects.
   *
   * The signature is used to decide whether a cached generated CMake project
   * must be reconfigured. It includes every option that can affect the produced
   * binary, including sanitizer mode and script/compiler/linker arguments.
   *
   * @param useVixRuntime True if the script links against the Vix runtime.
   * @param scriptFlags Compiler/linker flags forwarded to script mode.
   * @return Stable configuration signature string.
   */
  std::string make_script_config_signature(
      bool useVixRuntime,
      DiagnosticMode mode,
      const std::vector<std::string> &scriptFlags,
      const std::vector<std::string> &compilerArgs = {},
      const std::vector<std::string> &linkerArgs = {});

  /**
   * @brief Starts the watch-mode spinner.
   *
   * @param label Text displayed next to the spinner.
   */
  void watch_spinner_start(std::string label);

  void watch_spinner_finish();

  /**
   * @brief Stops the watch-mode spinner.
   */
  void watch_spinner_stop();

  /**
   * @brief Temporarily stops the watch spinner before printing output.
   */
  void watch_spinner_pause_for_output();

  /**
   * @brief Applies sanitizer-related runtime environment variables.
   *
   * On POSIX systems, this configures ASan, UBSan, or TSan environment
   * variables so sanitizer reports are deterministic and easier for Vix
   * to capture and convert into friendly diagnostics. On Windows this is a
   * no-op because the current runtime sanitizer environment is POSIX-only.
   *
   * @param mode Resolved Vix diagnostic mode.
   */
  void apply_sanitizer_env_if_needed(
      DiagnosticMode mode);

  /**
   * @brief Finds a precompiled Vix header if available.
   *
   * @return Path to the PCH file, or std::nullopt if not found.
   */
  std::optional<fs::path> find_vix_pch();

  /**
   * @brief Finds the installed Vix include directory.
   *
   * @return Path to the include directory, or std::nullopt if not found.
   */
  std::optional<fs::path> find_vix_include_dir();

  /**
   * @brief Finds the installed Vix library.
   *
   * @return Path to the Vix library, or std::nullopt if not found.
   */
  std::optional<fs::path> find_vix_lib();

  /**
   * @brief Finds all installed Vix module libraries.
   *
   * @return List of discovered Vix module library paths.
   */
  std::vector<fs::path> find_vix_all_module_libs();

} // namespace vix::commands::RunCommand::detail

#endif
