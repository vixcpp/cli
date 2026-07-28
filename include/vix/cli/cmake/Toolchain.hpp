/**
 *
 *  @file Toolchain.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_TOOLCHAIN_HPP
#define VIX_CLI_TOOLCHAIN_HPP

#include <vix/engine/Toolchain.hpp>

namespace vix::cli::build
{
  using vix::engine::detect_available_targets;
  using vix::engine::infer_processor_from_triple;
  using vix::engine::toolchain_contents_for_triple;
} // namespace vix::cli::build

#endif
