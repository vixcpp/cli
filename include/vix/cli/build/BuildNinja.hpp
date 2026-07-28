/**
 *
 *  @file BuildNinja.hpp
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
#ifndef VIX_CLI_BUILD_BUILD_NINJA_HPP
#define VIX_CLI_BUILD_BUILD_NINJA_HPP

#include <vix/engine/BuildNinja.hpp>

namespace vix::cli::build
{
  using vix::engine::classify_ninja_edge;
  using vix::engine::default_build_ninja_path;
  using vix::engine::NinjaBuildFile;
  using vix::engine::NinjaEdge;
  using vix::engine::NinjaEdgeKind;
  using vix::engine::NinjaRule;
  using vix::engine::parse_build_ninja_text;
  using vix::engine::read_build_ninja;
  using vix::engine::resolve_ninja_path;
  using vix::engine::to_string;
} // namespace vix::cli::build

#endif
