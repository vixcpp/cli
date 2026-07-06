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

namespace vix::commands::RunCommand::detail
{
  namespace fs = std::filesystem;

  /**
   * @brief Prints the watch-mode restart banner.
   *
   * @param path Path of the file that triggered the restart.
   * @param label Message displayed while rebuilding or restarting.
   */
  void print_watch_restart_banner(const fs::path &path, std::string_view label);

  /**
   * @brief Checks whether ASan+UBSan or UBSan-only mode is enabled.
   *
   * This helper intentionally does not include ThreadSanitizer.
   * For ASan, UBSan, and TSan together, use want_any_sanitizer()
   * from RunDetail.hpp.
   *
   * @param enableSanitizers True when ASan+UBSan mode is enabled.
   * @param enableUbsanOnly True when UBSan-only mode is enabled.
   * @return True if ASan+UBSan or UBSan-only mode is active.
   */
  bool want_sanitizers(
      bool enableSanitizers,
      bool enableUbsanOnly);

  /**
   * @brief Returns the sanitizer mode string used by generated CMake projects.
   *
   * Possible return values:
   * - "asan_ubsan"
   * - "ubsan"
   * - "tsan"
   * - "none"
   *
   * @param enableSanitizers True when ASan+UBSan mode is enabled.
   * @param enableUbsanOnly True when UBSan-only mode is enabled.
   * @param enableThreadSanitizer True when ThreadSanitizer mode is enabled.
   * @return Sanitizer mode string.
   */
  std::string sanitizer_mode_string(
      bool enableSanitizers,
      bool enableUbsanOnly,
      bool enableThreadSanitizer);

  /**
   * @brief Builds the configuration signature for generated script CMake projects.
   *
   * The signature is used to decide whether a cached generated CMake project
   * must be reconfigured. It includes every option that can affect the produced
   * binary, including sanitizer mode, script flags, and optional database support.
   *
   * @param useVixRuntime True if the script links against the Vix runtime.
   * @param enableSanitizers True when ASan+UBSan mode is enabled.
   * @param enableUbsanOnly True when UBSan-only mode is enabled.
   * @param enableThreadSanitizer True when ThreadSanitizer mode is enabled.
   * @param scriptFlags Compiler/linker flags forwarded to script mode.
   * @param withSqlite True if SQLite support is enabled.
   * @param withMySql True if MySQL support is enabled.
   * @return Stable configuration signature string.
   */
  std::string make_script_config_signature(
      bool useVixRuntime,
      bool enableSanitizers,
      bool enableUbsanOnly,
      bool enableThreadSanitizer,
      const std::vector<std::string> &scriptFlags,
      bool withSqlite,
      bool withMySql);

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
   * @param enableSanitizers True when ASan+UBSan mode is enabled.
   * @param enableUbsanOnly True when UBSan-only mode is enabled.
   * @param enableThreadSanitizer True when ThreadSanitizer mode is enabled.
   */
  void apply_sanitizer_env_if_needed(
      bool enableSanitizers,
      bool enableUbsanOnly,
      bool enableThreadSanitizer);

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
