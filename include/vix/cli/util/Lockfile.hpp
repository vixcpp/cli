/**
 *
 *  @file Lockfile.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_UTIL_LOCKFILE_HPP
#define VIX_CLI_UTIL_LOCKFILE_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace vix::cli::util::lockfile
{
  struct LockedDependency
  {
    std::string id;
    std::string requested;
    std::string version;
    std::string repo;
    std::string tag;
    std::string commit;
    std::string hash;
  };

  void write_lockfile_replace_all_or_throw(
      const std::filesystem::path &lockPath,
      const std::vector<LockedDependency> &dependencies);
}

#endif
