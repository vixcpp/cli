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

#ifndef VIX_CLI_BUILD_DEPENDENCY_FILE_HPP
#define VIX_CLI_BUILD_DEPENDENCY_FILE_HPP

#include <vix/engine/DependencyFile.hpp>

namespace vix::cli::build
{
  using vix::engine::dependency_file_for_object;
  using vix::engine::DependencyFile;
  using vix::engine::normalize_dependency_path;
  using vix::engine::parse_dependency_file_text;
  using vix::engine::read_dependency_file;
} // namespace vix::cli::build

#endif
