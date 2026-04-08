/**
 *
 *  @file Version.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_UTIL_VERSION_HPP
#define VIX_CLI_UTIL_VERSION_HPP

#include <string>
#include <vector>

namespace vix::cli::util
{
  std::vector<int> parseVersionParts(const std::string &version);
  int compareVersions(const std::string &lhs, const std::string &rhs);
  bool isVersionGreater(const std::string &lhs, const std::string &rhs);
  std::string findLatestVersionFromJsonObjectKeys(const std::vector<std::string> &versions);
}

#endif
