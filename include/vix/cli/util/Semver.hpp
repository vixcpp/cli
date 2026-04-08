/**
 *
 *  @file Semver.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_UTIL_SEMVER_HPP
#define VIX_CLI_UTIL_SEMVER_HPP

#include <optional>
#include <string>
#include <vector>

namespace vix::cli::util::semver
{
  int compare(const std::string &lhs, const std::string &rhs);
  bool satisfies(const std::string &version, const std::string &range);
  std::optional<std::string> resolveMaxSatisfying(
      const std::vector<std::string> &versions,
      const std::string &range);
  std::string findLatest(const std::vector<std::string> &versions);
  void sortAscending(std::vector<std::string> &versions);
  void sortDescending(std::vector<std::string> &versions);
}

#endif
