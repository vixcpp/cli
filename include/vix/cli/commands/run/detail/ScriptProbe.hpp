/**
 *
 *  @file ScriptProbe.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_CLI_SCRIPT_PROBE_HPP
#define VIX_CLI_SCRIPT_PROBE_HPP

#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/commands/run/RunDetail.hpp>

namespace vix::commands::RunCommand::detail
{
  namespace fs = std::filesystem;

  /**
   * @brief Detect whether a .cpp script uses the Vix runtime.
   *
   * This is a lightweight compatibility helper used by both the probe
   * and the CMake fallback path.
   */
  bool script_uses_vix(const fs::path &cppPath);

  /**
   * @brief Detect high-level features used by a C++ script.
   *
   * The probe remains intentionally heuristic and conservative.
   * When it detects features that typically require Vix targets,
   * ORM, DB, or MySQL integration, it will prefer CMake fallback.
   */
  ScriptFeatures detect_script_features(const fs::path &cppPath);

  /**
   * @brief Parse compile flags forwarded to script mode.
   *
   * Supported direct-compile compile flags are extracted into structured
   * buckets such as include directories, defines, and compiler options.
   */
  ScriptCompileFlags parse_compile_flags(const std::vector<std::string> &flags);

  /**
   * @brief Parse link flags forwarded to script mode.
   *
   * Supported direct-compile link flags are extracted into structured
   * buckets such as libraries, library directories, and linker options.
   */
  ScriptLinkFlags parse_link_flags(const std::vector<std::string> &flags);

  /**
   * @brief Probe a single C++ script and choose the execution strategy.
   *
   * The result is conservative by design:
   * - direct compile is chosen only for simple, robust cases
   * - generated CMake is selected as fallback for complex or ambiguous cases
   */
  ScriptProbeResult probe_single_cpp_script(const Options &opt);

} // namespace vix::commands::RunCommand::detail

#endif
