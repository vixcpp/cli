/**
 *
 *  @file CMakeBuild.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_CMAKE_BUILD_HPP
#define VIX_CLI_CMAKE_BUILD_HPP

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vix/cli/process/Process.hpp>

namespace vix::cli::build
{
  namespace fs = std::filesystem;
  namespace process = vix::cli::process;

  bool is_cmake_configure_summary_line(std::string_view line);
  bool is_configure_cmd(const std::vector<std::string> &argv);
  int default_jobs();

  std::vector<std::string> cmake_configure_argv(
      const process::Plan &plan,
      const process::Options &opt);

  std::vector<std::string> cmake_build_argv(
      const process::Plan &plan,
      const process::Options &opt);

  std::vector<std::string> ninja_dry_run_argv(
      const process::Plan &plan,
      const process::Options &opt);

  std::vector<std::pair<std::string, std::string>>
  ninja_env(const process::Options &opt, const process::Plan &plan);

  process::ExecResult run_process_capture(
      const std::vector<std::string> &argv,
      const std::vector<std::pair<std::string, std::string>> &extraEnv,
      std::string &outText);

  process::ExecResult run_process_live_to_log(
      const std::vector<std::string> &argv,
      const std::vector<std::pair<std::string, std::string>> &extraEnv,
      const fs::path &logPath,
      bool quiet,
      bool cmakeVerbose,
      bool progressOnly);

  bool ninja_is_up_to_date(const process::Options &opt, const process::Plan &plan);

  void print_preset_summary(const process::Options &opt, const process::Plan &plan);

} // namespace vix::cli::build

#endif
