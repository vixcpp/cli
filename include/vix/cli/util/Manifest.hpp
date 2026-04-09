/**
 *
 *  @file Manifest.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_UTIL_MANIFEST_HPP
#define VIX_CLI_UTIL_MANIFEST_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace vix::cli::util::manifest
{
  struct Dependency
  {
    std::string id;
    std::string requested;
  };

  struct Manifest
  {
    std::vector<Dependency> dependencies;
  };

  Manifest read_manifest_or_throw(const std::filesystem::path &manifestPath);

  std::vector<Dependency> read_manifest_dependencies_or_throw(
      const std::filesystem::path &manifestPath);

  void write_manifest_or_throw(
      const std::filesystem::path &manifestPath,
      const Manifest &manifest);

  void upsert_manifest_dependency_or_throw(
      const std::filesystem::path &manifestPath,
      const Dependency &dependency);
}

#endif
