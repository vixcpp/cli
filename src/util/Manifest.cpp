/**
 *
 *  @file Manifest.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/util/Manifest.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::cli::util::manifest
{
  namespace
  {
    json read_json_or_throw(const fs::path &path)
    {
      std::ifstream in(path);
      if (!in)
      {
        throw std::runtime_error("cannot open file: " + path.string());
      }

      json j;
      in >> j;
      return j;
    }

    void write_json_or_throw(const fs::path &path, const json &j)
    {
      std::ofstream out(path);
      if (!out)
      {
        throw std::runtime_error("cannot write file: " + path.string());
      }

      out << j.dump(2) << "\n";
    }

    Dependency parse_dependency_or_throw(const json &item)
    {
      if (!item.is_object())
      {
        throw std::runtime_error("invalid vix.json: dependency entry must be an object");
      }

      const std::string id = item.value("id", "");
      const std::string requested = item.value("version", "");

      if (id.empty())
      {
        throw std::runtime_error("invalid vix.json: dependency id cannot be empty");
      }

      if (requested.empty())
      {
        throw std::runtime_error("invalid vix.json: dependency version cannot be empty");
      }

      return Dependency{id, requested};
    }

    json dependency_to_json(const Dependency &dependency)
    {
      return json{
          {"id", dependency.id},
          {"version", dependency.requested},
      };
    }
  }

  Manifest read_manifest_or_throw(const fs::path &manifestPath)
  {
    Manifest manifest;

    if (!fs::exists(manifestPath))
    {
      return manifest;
    }

    const json root = read_json_or_throw(manifestPath);

    if (!root.is_object())
    {
      throw std::runtime_error("invalid vix.json: root must be an object");
    }

    if (!root.contains("deps"))
    {
      return manifest;
    }

    if (!root["deps"].is_array())
    {
      throw std::runtime_error("invalid vix.json: deps must be an array");
    }

    for (const auto &item : root["deps"])
    {
      manifest.dependencies.push_back(parse_dependency_or_throw(item));
    }

    return manifest;
  }

  std::vector<Dependency> read_manifest_dependencies_or_throw(
      const fs::path &manifestPath)
  {
    return read_manifest_or_throw(manifestPath).dependencies;
  }

  void write_manifest_or_throw(
      const fs::path &manifestPath,
      const Manifest &manifest)
  {
    json root = json::object();
    root["deps"] = json::array();

    for (const auto &dependency : manifest.dependencies)
    {
      if (dependency.id.empty())
      {
        throw std::runtime_error("manifest dependency id cannot be empty");
      }

      if (dependency.requested.empty())
      {
        throw std::runtime_error("manifest dependency requested version cannot be empty");
      }

      root["deps"].push_back(dependency_to_json(dependency));
    }

    write_json_or_throw(manifestPath, root);
  }

  void upsert_manifest_dependency_or_throw(
      const fs::path &manifestPath,
      const Dependency &dependency)
  {
    if (dependency.id.empty())
    {
      throw std::runtime_error("manifest dependency id cannot be empty");
    }

    if (dependency.requested.empty())
    {
      throw std::runtime_error("manifest dependency requested version cannot be empty");
    }

    Manifest manifest = read_manifest_or_throw(manifestPath);

    bool updated = false;

    for (auto &item : manifest.dependencies)
    {
      if (item.id == dependency.id)
      {
        item.requested = dependency.requested;
        updated = true;
        break;
      }
    }

    if (!updated)
    {
      manifest.dependencies.push_back(dependency);
    }

    write_manifest_or_throw(manifestPath, manifest);
  }
}
