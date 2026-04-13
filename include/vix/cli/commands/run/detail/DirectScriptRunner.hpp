/**
 *
 *  @file DirectScriptRunner.hpp
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
#ifndef VIX_CLI_DIRECT_SCRIPT_RUNNER_HPP
#define VIX_CLI_DIRECT_SCRIPT_RUNNER_HPP

#include <filesystem>
#include <string>

#include <vix/cli/commands/run/RunDetail.hpp>

namespace vix::commands::RunCommand::detail
{
  namespace fs = std::filesystem;

  /**
   * @brief Return the global cache root used for directly compiled scripts.
   */
  fs::path get_direct_scripts_cache_root();

  /**
   * @brief Compute the cache key used for a directly compiled script.
   */
  std::string make_direct_script_cache_key(
      const fs::path &cppPath,
      const ScriptProbeResult &probe,
      const Options &opt);

  /**
   * @brief Load cache metadata for a direct script plan.
   */
  DirectScriptCacheState load_direct_script_cache_state(const DirectScriptPlan &plan);

  /**
   * @brief Build a direct compile plan for a probed script.
   */
  DirectScriptPlan make_direct_script_plan(
      const Options &opt,
      const ScriptProbeResult &probe);

  /**
   * @brief Execute a single C++ file with the fast direct-compile engine.
   */
  int run_single_cpp_direct(const Options &opt, const DirectScriptPlan &plan);

} // namespace vix::commands::RunCommand::detail

#endif
