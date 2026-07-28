/**
 *
 *  @file BuildContext.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Shared build context helpers
 *
 */

#ifndef VIX_CLI_BUILD_BUILD_CONTEXT_HPP
#define VIX_CLI_BUILD_BUILD_CONTEXT_HPP

#include <filesystem>
#include <optional>
#include <string>

#include <vix/engine/BuildContext.hpp>

#include <vix/cli/process/Process.hpp>

namespace vix::cli::build
{
  namespace fs = std::filesystem;
  namespace process = vix::cli::process;

  using vix::engine::BuildTargetOptions;
  using vix::engine::resolve_builtin_preset;

  inline std::string default_build_target_name(
      const process::Options &opt,
      const process::Plan &plan)
  {
    return vix::engine::default_build_target_name(opt.buildTarget, plan);
  }

  inline std::string default_graph_target_name(
      const process::Options &opt,
      const process::Plan &plan)
  {
    return vix::engine::default_graph_target_name(opt.buildTarget, plan);
  }

  inline fs::path default_project_executable_path(
      const process::Options &opt,
      const process::Plan &plan)
  {
    return vix::engine::default_project_executable_path(opt.buildTarget, plan);
  }

} // namespace vix::cli::build

#endif
