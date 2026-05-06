/**
 *
 *  @file BuildNode.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Build graph node model
 *
 */

#include <vix/cli/build/BuildNode.hpp>

#include <algorithm>
#include <sstream>
#include <system_error>

namespace vix::cli::build
{
  namespace
  {
    static std::string normalize_path_for_id(const fs::path &path)
    {
      fs::path normalized = path.lexically_normal();
      return normalized.generic_string();
    }

    static std::uint64_t file_size_or_zero(const fs::path &path)
    {
      std::error_code ec;
      const auto size = fs::file_size(path, ec);

      if (ec)
        return 0;

      return static_cast<std::uint64_t>(size);
    }

    static std::uint64_t file_mtime_or_zero(const fs::path &path)
    {
      std::error_code ec;
      const auto time = fs::last_write_time(path, ec);

      if (ec)
        return 0;

      return static_cast<std::uint64_t>(time.time_since_epoch().count());
    }
  } // namespace

  std::string to_string(BuildNodeKind kind)
  {
    switch (kind)
    {
    case BuildNodeKind::Source:
      return "source";
    case BuildNodeKind::Header:
      return "header";
    case BuildNodeKind::Config:
      return "config";
    case BuildNodeKind::Package:
      return "package";
    case BuildNodeKind::Object:
      return "object";
    case BuildNodeKind::Library:
      return "library";
    case BuildNodeKind::Executable:
      return "executable";
    case BuildNodeKind::Unknown:
    default:
      return "unknown";
    }
  }

  std::string to_string(BuildNodeState state)
  {
    switch (state)
    {
    case BuildNodeState::Clean:
      return "clean";
    case BuildNodeState::Dirty:
      return "dirty";
    case BuildNodeState::Missing:
      return "missing";
    default:
      return "dirty";
    }
  }

  BuildNodeKind build_node_kind_from_string(const std::string &value)
  {
    if (value == "source")
      return BuildNodeKind::Source;
    if (value == "header")
      return BuildNodeKind::Header;
    if (value == "config")
      return BuildNodeKind::Config;
    if (value == "package")
      return BuildNodeKind::Package;
    if (value == "object")
      return BuildNodeKind::Object;
    if (value == "library")
      return BuildNodeKind::Library;
    if (value == "executable")
      return BuildNodeKind::Executable;

    return BuildNodeKind::Unknown;
  }

  BuildNodeState build_node_state_from_string(const std::string &value)
  {
    if (value == "clean")
      return BuildNodeState::Clean;
    if (value == "missing")
      return BuildNodeState::Missing;
    if (value == "dirty")
      return BuildNodeState::Dirty;

    return BuildNodeState::Dirty;
  }

  bool BuildNode::valid() const
  {
    return !id.empty();
  }

  bool BuildNode::clean() const
  {
    return state == BuildNodeState::Clean;
  }

  bool BuildNode::dirty() const
  {
    return state == BuildNodeState::Dirty;
  }

  bool BuildNode::missing() const
  {
    return state == BuildNodeState::Missing;
  }

  void BuildNode::mark_clean()
  {
    state = BuildNodeState::Clean;
  }

  void BuildNode::mark_dirty()
  {
    state = BuildNodeState::Dirty;
  }

  void BuildNode::mark_missing()
  {
    state = BuildNodeState::Missing;
  }

  void BuildNode::add_dependency(const std::string &depId)
  {
    if (depId.empty())
      return;

    if (has_dependency(depId))
      return;

    deps.push_back(depId);
  }

  bool BuildNode::has_dependency(const std::string &depId) const
  {
    return std::find(deps.begin(), deps.end(), depId) != deps.end();
  }

  std::string make_build_node_id(BuildNodeKind kind, const fs::path &path)
  {
    std::ostringstream oss;
    oss << to_string(kind) << ":";
    oss << normalize_path_for_id(path);
    return oss.str();
  }

  BuildNode make_file_build_node(BuildNodeKind kind, const fs::path &path)
  {
    BuildNode node;
    node.kind = kind;
    node.state = BuildNodeState::Dirty;
    node.path = path.lexically_normal();
    node.id = make_build_node_id(kind, node.path);
    node.size = file_size_or_zero(node.path);
    node.mtime = file_mtime_or_zero(node.path);

    if (!fs::exists(node.path))
      node.state = BuildNodeState::Missing;

    return node;
  }

} // namespace vix::cli::build
