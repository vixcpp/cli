/**
 *
 *  @file Version.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/util/Version.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace vix::cli::util
{
  std::vector<int> parseVersionParts(const std::string &version)
  {
    std::vector<int> parts;
    std::string current;

    for (char c : version)
    {
      if (c == '.')
      {
        if (!current.empty())
        {
          parts.push_back(std::stoi(current));
          current.clear();
        }
        continue;
      }

      if (!std::isdigit(static_cast<unsigned char>(c)))
        throw std::runtime_error("invalid version segment: " + version);

      current += c;
    }

    if (!current.empty())
      parts.push_back(std::stoi(current));

    return parts;
  }

  int compareVersions(const std::string &lhs, const std::string &rhs)
  {
    const std::vector<int> left = parseVersionParts(lhs);
    const std::vector<int> right = parseVersionParts(rhs);

    const std::size_t maxSize = std::max(left.size(), right.size());

    for (std::size_t i = 0; i < maxSize; ++i)
    {
      const int l = (i < left.size()) ? left[i] : 0;
      const int r = (i < right.size()) ? right[i] : 0;

      if (l < r)
        return -1;

      if (l > r)
        return 1;
    }

    return 0;
  }

  bool isVersionGreater(const std::string &lhs, const std::string &rhs)
  {
    return compareVersions(lhs, rhs) > 0;
  }

  std::string findLatestVersionFromJsonObjectKeys(const std::vector<std::string> &versions)
  {
    std::string best;

    for (const std::string &version : versions)
    {
      if (best.empty() || isVersionGreater(version, best))
        best = version;
    }

    return best;
  }
}
