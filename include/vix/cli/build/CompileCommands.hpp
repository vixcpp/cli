/**
 *
 *  @file BuildStyle.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Reusable build output style
 *
 */
#ifndef VIX_CLI_BUILD_COMPILE_COMMANDS_HPP
#define VIX_CLI_BUILD_COMPILE_COMMANDS_HPP

#include <vix/engine/CompileCommands.hpp>

namespace vix::cli::build
{
  using vix::engine::CompileCommandEntry;
  using vix::engine::default_compile_commands_path;
  using vix::engine::extract_compile_output_path;
  using vix::engine::parse_compile_commands_text;
  using vix::engine::read_compile_commands;
  using vix::engine::resolve_compile_command_path;
  using vix::engine::split_compile_command;
} // namespace vix::cli::build

#endif
