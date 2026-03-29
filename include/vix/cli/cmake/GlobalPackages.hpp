/**
 *
 *  @file GlobalPackages.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_CMAKE_GLOBALPACKAGES_HPP
#define VIX_CLI_CMAKE_GLOBALPACKAGES_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  struct GlobalPackage
  {
    std::string id;
    std::string pkgDir;
    std::string includeDir{"include"};
    std::string type{"header-only"};
    fs::path installedPath;
  };

  std::vector<GlobalPackage> load_global_packages();

  std::string make_global_packages_cmake(
      const std::vector<GlobalPackage> &packages);

} // namespace vix::cli::build

#endif
