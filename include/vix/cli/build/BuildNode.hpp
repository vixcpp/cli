/**
 *
 *  @file BuildNode.hpp
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

#ifndef VIX_CLI_BUILD_BUILD_NODE_HPP
#define VIX_CLI_BUILD_BUILD_NODE_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  /**
   * @brief Type of node stored inside the Vix build graph.
   *
   * A build node represents one stable item that can affect the build.
   * It can be a source file, a header, a generated object file, a static
   * library, an executable, a package, or a configuration file.
   */
  enum class BuildNodeKind
  {
    Unknown,   ///< Unknown or unsupported node type
    Source,    ///< C/C++ source file, ex: main.cpp
    Header,    ///< C/C++ header file, ex: app.hpp
    Config,    ///< Build configuration file, ex: CMakeLists.txt
    Package,   ///< External or Vix package dependency
    Object,    ///< Compiled object file, ex: main.o
    Library,   ///< Static/shared library output
    Executable ///< Final executable output
  };

  /**
   * @brief State of a build node during incremental build analysis.
   *
   * Vix uses this state to decide whether a node can be reused from cache
   * or whether it must be rebuilt.
   */
  enum class BuildNodeState
  {
    Clean,  ///< Node is unchanged and can be reused
    Dirty,  ///< Node or one of its dependencies changed
    Missing ///< Node output is missing from disk
  };

  /**
   * @brief Convert a build node kind to a stable string.
   *
   * @param kind Node kind
   * @return Stable lowercase string representation
   */
  std::string to_string(BuildNodeKind kind);

  /**
   * @brief Convert a build node state to a stable string.
   *
   * @param state Node state
   * @return Stable lowercase string representation
   */
  std::string to_string(BuildNodeState state);

  /**
   * @brief Parse a build node kind from a stable string.
   *
   * Unknown strings are parsed as BuildNodeKind::Unknown.
   *
   * @param value String value
   * @return Parsed node kind
   */
  BuildNodeKind build_node_kind_from_string(const std::string &value);

  /**
   * @brief Parse a build node state from a stable string.
   *
   * Unknown strings are parsed as BuildNodeState::Dirty to keep the build safe.
   *
   * @param value String value
   * @return Parsed node state
   */
  BuildNodeState build_node_state_from_string(const std::string &value);

  /**
   * @brief One node inside the Vix build graph.
   *
   * A BuildNode is intentionally small and deterministic. It stores:
   *   - a stable id
   *   - a kind
   *   - a path
   *   - size and mtime metadata
   *   - a content/config hash
   *   - dependency node ids
   *
   * The node does not execute work by itself. Execution is handled later by
   * BuildTask and BuildScheduler.
   */
  struct BuildNode
  {
    std::string id;                              ///< Stable node id
    BuildNodeKind kind{BuildNodeKind::Unknown};  ///< Node kind
    BuildNodeState state{BuildNodeState::Dirty}; ///< Current dirty state

    fs::path path;    ///< Filesystem path when applicable
    std::string hash; ///< Stable content/config hash

    std::uint64_t size{0};  ///< File size in bytes
    std::uint64_t mtime{0}; ///< Last write timestamp count

    std::vector<std::string> deps; ///< Dependency node ids

    /**
     * @brief Check whether this node has a valid id.
     *
     * @return true if id is not empty
     */
    bool valid() const;

    /**
     * @brief Check whether this node is clean.
     *
     * @return true if state is BuildNodeState::Clean
     */
    bool clean() const;

    /**
     * @brief Check whether this node is dirty.
     *
     * @return true if state is BuildNodeState::Dirty
     */
    bool dirty() const;

    /**
     * @brief Check whether this node output/input is missing.
     *
     * @return true if state is BuildNodeState::Missing
     */
    bool missing() const;

    /**
     * @brief Mark this node as clean.
     */
    void mark_clean();

    /**
     * @brief Mark this node as dirty.
     */
    void mark_dirty();

    /**
     * @brief Mark this node as missing.
     */
    void mark_missing();

    /**
     * @brief Add a dependency node id if it is not already present.
     *
     * Empty dependency ids are ignored.
     *
     * @param depId Dependency node id
     */
    void add_dependency(const std::string &depId);

    /**
     * @brief Check whether this node depends on another node id.
     *
     * @param depId Dependency node id
     * @return true if depId is already registered
     */
    bool has_dependency(const std::string &depId) const;
  };

  /**
   * @brief Create a stable node id from a node kind and path.
   *
   * The returned id is deterministic and uses normalized generic paths.
   *
   * @param kind Node kind
   * @param path Node path
   * @return Stable node id
   */
  std::string make_build_node_id(BuildNodeKind kind, const fs::path &path);

  /**
   * @brief Create a BuildNode from filesystem metadata.
   *
   * This function reads file size and mtime. It does not hash file content.
   * Hashing is handled later by BuildGraph or ObjectCache.
   *
   * @param kind Node kind
   * @param path File path
   * @return Build node with id, path, size and mtime populated
   */
  BuildNode make_file_build_node(BuildNodeKind kind, const fs::path &path);

} // namespace vix::cli::build

#endif
