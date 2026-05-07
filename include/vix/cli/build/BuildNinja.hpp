/**
 *
 *  @file BuildNinja.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Ninja build graph parser
 *
 */

#ifndef VIX_CLI_BUILD_BUILD_NINJA_HPP
#define VIX_CLI_BUILD_BUILD_NINJA_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  enum class NinjaEdgeKind
  {
    Unknown,
    Compile,
    Archive,
    Link,
    Copy,
    Install,
    Utility
  };

  struct NinjaRule
  {
    std::string name;
    std::unordered_map<std::string, std::string> variables;

    bool valid() const;
  };

  struct NinjaEdge
  {
    std::vector<fs::path> outputs;
    std::vector<fs::path> explicitInputs;
    std::vector<fs::path> implicitInputs;
    std::vector<fs::path> orderOnlyInputs;

    std::string rule;
    NinjaEdgeKind kind{NinjaEdgeKind::Unknown};

    std::unordered_map<std::string, std::string> variables;

    bool valid() const;
    fs::path primary_output() const;
  };

  struct NinjaBuildFile
  {
    fs::path path;
    fs::path directory;

    std::unordered_map<std::string, std::string> variables;
    std::unordered_map<std::string, NinjaRule> rules;
    std::vector<NinjaEdge> edges;

    bool valid() const;
  };

  std::string to_string(NinjaEdgeKind kind);

  NinjaEdgeKind classify_ninja_edge(
      const NinjaEdge &edge,
      const NinjaRule *rule);

  fs::path default_build_ninja_path(const fs::path &buildDir);

  std::optional<NinjaBuildFile> parse_build_ninja_text(
      const std::string &text,
      const fs::path &path = {});

  std::optional<NinjaBuildFile> read_build_ninja(
      const fs::path &path);

  fs::path resolve_ninja_path(
      const fs::path &base,
      const fs::path &path);

} // namespace vix::cli::build

#endif
