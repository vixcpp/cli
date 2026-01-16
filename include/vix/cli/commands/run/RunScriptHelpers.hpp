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
#include <string>
#include <string_view>
#include <vector>

namespace vix::commands::RunCommand::detail
{
  namespace fs = std::filesystem;

  // UI
  void print_watch_restart_banner(const fs::path &path, std::string_view label);
  std::string sanitizer_mode_string(bool enableSanitizers, bool enableUbsanOnly);
  bool want_sanitizers(bool enableSanitizers, bool enableUbsanOnly);
  std::string make_script_config_signature(
      bool useVixRuntime,
      bool enableSanitizers,
      bool enableUbsanOnly,
      const std::vector<std::string> &scriptFlags);

  void watch_spinner_start(std::string label);
  void watch_spinner_stop();
  void watch_spinner_pause_for_output();

#ifndef _WIN32
  void apply_sanitizer_env_if_needed(bool enableSanitizers, bool enableUbsanOnly);
#endif
} // namespace vix::commands::RunCommand::detail

#endif
