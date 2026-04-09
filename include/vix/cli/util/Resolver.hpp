/**
 *
 *  @file Resolver.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_UTIL_RESOLVER_HPP
#define VIX_CLI_UTIL_RESOLVER_HPP

#include <vix/cli/util/Lockfile.hpp>
#include <vix/cli/util/Manifest.hpp>

#include <vector>

namespace vix::cli::util::resolver
{
  std::vector<vix::cli::util::lockfile::LockedDependency>
  resolve_project_dependencies_or_throw(
      const std::vector<vix::cli::util::manifest::Dependency> &manifestDependencies);
}

#endif
