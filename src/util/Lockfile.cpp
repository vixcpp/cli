/**
 *
 *  @file Lockfile.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/util/Lockfile.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::cli::util::lockfile
{
  namespace
  {
    json dependency_to_json(const LockedDependency &dependency)
    {
      return json{
          {"id", dependency.id},
          {"requested", dependency.requested},
          {"version", dependency.version},
          {"repo", dependency.repo},
          {"tag", dependency.tag},
          {"commit", dependency.commit},
          {"hash", dependency.hash},
      };
    }
  }

  void write_lockfile_replace_all_or_throw(
      const fs::path &lockPath,
      const std::vector<LockedDependency> &dependencies)
  {
    json root = json::object();
    root["lockVersion"] = 1;
    root["dependencies"] = json::array();

    std::vector<LockedDependency> sortedDependencies = dependencies;

    std::sort(sortedDependencies.begin(), sortedDependencies.end(),
              [](const LockedDependency &left, const LockedDependency &right)
              {
                return left.id < right.id;
              });

    for (const auto &dependency : sortedDependencies)
    {
      if (dependency.id.empty())
      {
        throw std::runtime_error("lock dependency id cannot be empty");
      }

      if (dependency.version.empty())
      {
        throw std::runtime_error("lock dependency version cannot be empty");
      }

      root["dependencies"].push_back(dependency_to_json(dependency));
    }

    std::ofstream out(lockPath);
    if (!out)
    {
      throw std::runtime_error("cannot write file: " + lockPath.string());
    }

    out << root.dump(2) << "\n";
  }
}
