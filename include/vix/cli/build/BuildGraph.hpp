/**
 *
 *  @file BuildGraph.hpp
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

#ifndef VIX_CLI_BUILD_BUILD_GRAPH_HPP
#define VIX_CLI_BUILD_BUILD_GRAPH_HPP

#include <vix/engine/BuildGraph.hpp>

namespace vix::cli::build
{
  using vix::engine::build_node_kind_from_string;
  using vix::engine::build_node_state_from_string;
  using vix::engine::build_task_kind_from_string;
  using vix::engine::build_task_state_from_string;
  using vix::engine::BuildGraph;
  using vix::engine::BuildGraphConfig;
  using vix::engine::BuildGraphInvalidationResult;
  using vix::engine::BuildGraphScanResult;
  using vix::engine::BuildNode;
  using vix::engine::BuildNodeKind;
  using vix::engine::BuildNodeState;
  using vix::engine::BuildTask;
  using vix::engine::BuildTaskKind;
  using vix::engine::BuildTaskState;
  using vix::engine::classify_ninja_edge;
  using vix::engine::CompileCommandEntry;
  using vix::engine::default_build_ninja_path;
  using vix::engine::default_compile_commands_path;
  using vix::engine::dependency_file_for_object;
  using vix::engine::DependencyFile;
  using vix::engine::extract_compile_output_path;
  using vix::engine::hash_build_command;
  using vix::engine::make_archive_task;
  using vix::engine::make_build_node_id;
  using vix::engine::make_build_task_id;
  using vix::engine::make_compile_task;
  using vix::engine::make_file_build_node;
  using vix::engine::make_link_task;
  using vix::engine::NinjaBuildFile;
  using vix::engine::NinjaEdge;
  using vix::engine::NinjaEdgeKind;
  using vix::engine::NinjaRule;
  using vix::engine::normalize_dependency_path;
  using vix::engine::parse_build_ninja_text;
  using vix::engine::parse_compile_commands_text;
  using vix::engine::parse_dependency_file_text;
  using vix::engine::read_build_ninja;
  using vix::engine::read_compile_commands;
  using vix::engine::read_dependency_file;
  using vix::engine::resolve_compile_command_path;
  using vix::engine::resolve_ninja_path;
  using vix::engine::split_compile_command;
  using vix::engine::to_string;
} // namespace vix::cli::build

#endif
