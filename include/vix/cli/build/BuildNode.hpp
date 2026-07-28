/**
 *
 *  @file BuildNode.hpp
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
#ifndef VIX_CLI_BUILD_BUILD_NODE_HPP
#define VIX_CLI_BUILD_BUILD_NODE_HPP

#include <vix/engine/BuildNode.hpp>

namespace vix::cli::build
{
  using vix::engine::build_node_kind_from_string;
  using vix::engine::build_node_state_from_string;
  using vix::engine::BuildNode;
  using vix::engine::BuildNodeKind;
  using vix::engine::BuildNodeState;
  using vix::engine::make_build_node_id;
  using vix::engine::make_file_build_node;
  using vix::engine::to_string;
} // namespace vix::cli::build

#endif
