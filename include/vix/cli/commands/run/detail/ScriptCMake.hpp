/**
 *
 *  @file ScriptCMake.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by an MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_CLI_SCRIPT_CMAKE_HPP
#define VIX_CLI_SCRIPT_CMAKE_HPP

#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/commands/run/RunDetail.hpp>

namespace vix::commands::RunCommand::detail
{
  namespace fs = std::filesystem;

  /**
   * @brief Return the root directory used for generated fallback CMake script projects.
   *
   * This directory is used only when the direct script path is not suitable and
   * `vix run` must fall back to a generated CMake project.
   */
  fs::path get_scripts_root(bool localCache);

  /**
   * @brief Generate the CMakeLists.txt content for the fallback script engine.
   *
   * This function is used only by the generated CMake fallback path. The direct
   * compile path does not depend on this file.
   */
  std::string make_script_cmakelists(
      const std::string &exeName,
      const fs::path &cppPath,
      bool useVixRuntime,
      const std::vector<std::string> &scriptFlags,
      bool withSqlite,
      bool withMySql);

} // namespace vix::commands::RunCommand::detail

#endif
