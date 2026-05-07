/**
 *
 *  @file BuildGraph.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Incremental build graph
 *
 */

#ifndef VIX_CLI_BUILD_BUILD_GRAPH_HPP
#define VIX_CLI_BUILD_BUILD_GRAPH_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <vix/cli/build/BuildNode.hpp>
#include <vix/cli/build/BuildTask.hpp>
#include <vix/cli/build/CompileCommands.hpp>
#include <vix/cli/build/DependencyFile.hpp>

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  /**
   * @brief Configuration used to scan and build a project graph.
   *
   * This structure tells Vix where the project lives, where generated build
   * files should be stored, which compiler command should be used, and which
   * global build fingerprint should be attached to object cache keys.
   */
  struct BuildGraphConfig
  {
    fs::path projectDir; ///< Project root directory
    fs::path buildDir;   ///< Build directory used by Vix
    fs::path objectDir;  ///< Directory where object files are generated

    std::string compiler{"c++"};  ///< Compiler executable
    std::string buildFingerprint; ///< Global build/config fingerprint

    std::vector<std::string> includeDirs; ///< Include directories
    std::vector<std::string> defines;     ///< Preprocessor defines
    std::vector<std::string> flags;       ///< Extra compiler flags

    /**
     * @brief Check whether the configuration has enough data.
     *
     * @return true if projectDir and buildDir are not empty
     */
    bool valid() const;
  };

  /**
   * @brief Summary returned after scanning a project graph.
   */
  struct BuildGraphScanResult
  {
    std::size_t sources{0}; ///< Number of source files found
    std::size_t headers{0}; ///< Number of header files found
    std::size_t configs{0}; ///< Number of config files found
    std::size_t tasks{0};   ///< Number of generated tasks
  };

  /**
   * @brief Incremental build graph for Vix projects.
   *
   * BuildGraph is the central model used by the new Vix build architecture.
   *
   * It stores:
   *   - BuildNode entries for sources, headers, config files, objects and outputs
   *   - BuildTask entries for compile/link/archive actions
   *   - dependency links extracted from compiler .d files
   *
   * The graph is designed to support:
   *   - dirty/clean analysis
   *   - object cache reuse
   *   - parallel task execution
   *   - deterministic rebuild decisions
   */
  class BuildGraph
  {
  public:
    /**
     * @brief Create an empty build graph.
     */
    BuildGraph() = default;

    /**
     * @brief Create a build graph with a configuration.
     *
     * @param config Build graph configuration
     */
    explicit BuildGraph(BuildGraphConfig config);

    /**
     * @brief Return the build graph configuration.
     *
     * @return Build graph configuration
     */
    const BuildGraphConfig &config() const;

    /**
     * @brief Set the build graph configuration.
     *
     * @param config Build graph configuration
     */
    void set_config(BuildGraphConfig config);

    /**
     * @brief Remove all nodes and tasks.
     */
    void clear();

    /**
     * @brief Check whether the graph is empty.
     *
     * @return true if there are no nodes and no tasks
     */
    bool empty() const;

    /**
     * @brief Add or replace a build node.
     *
     * @param node Build node
     * @return true if the node was valid and stored
     */
    bool add_node(const BuildNode &node);

    /**
     * @brief Add or replace a build task.
     *
     * @param task Build task
     * @return true if the task was valid and stored
     */
    bool add_task(const BuildTask &task);

    /**
     * @brief Find a node by id.
     *
     * @param id Node id
     * @return Node pointer, or nullptr if not found
     */
    BuildNode *find_node(const std::string &id);

    /**
     * @brief Find a node by id.
     *
     * @param id Node id
     * @return Node pointer, or nullptr if not found
     */
    const BuildNode *find_node(const std::string &id) const;

    /**
     * @brief Find a task by id.
     *
     * @param id Task id
     * @return Task pointer, or nullptr if not found
     */
    BuildTask *find_task(const std::string &id);

    /**
     * @brief Find a task by id.
     *
     * @param id Task id
     * @return Task pointer, or nullptr if not found
     */
    const BuildTask *find_task(const std::string &id) const;

    /**
     * @brief Return all nodes.
     *
     * @return Node map
     */
    const std::unordered_map<std::string, BuildNode> &nodes() const;

    /**
     * @brief Return all tasks.
     *
     * @return Task map
     */
    const std::unordered_map<std::string, BuildTask> &tasks() const;

    /**
     * @brief Return node ids in deterministic order.
     *
     * @return Sorted node ids
     */
    std::vector<std::string> sorted_node_ids() const;

    /**
     * @brief Return task ids in deterministic order.
     *
     * @return Sorted task ids
     */
    std::vector<std::string> sorted_task_ids() const;

    /**
     * @brief Scan the project directory and generate source/header/config nodes.
     *
     * This does not run the compiler. It only discovers files and creates
     * initial compile tasks.
     *
     * @return Scan summary
     */
    BuildGraphScanResult scan_project();

    /**
     * @brief Load compile tasks from compile_commands.json.
     *
     * This imports the real compiler commands generated by CMake/Ninja and
     * replaces guessed scan_project() compile commands with exact argv, working
     * directory and object output paths.
     *
     * @param path Path to compile_commands.json
     * @return Number of imported compile commands
     */
    std::size_t load_compile_commands(const fs::path &path);

    /**
     * @brief Load dependency files from the object directory and connect nodes.
     *
     * This reads .d files generated by GCC/Clang and links source/header
     * dependencies into the graph.
     */
    void load_dependency_files();

    /**
     * @brief Mark all nodes dirty.
     */
    void mark_all_dirty();

    /**
     * @brief Mark nodes clean when their metadata matches a previous graph.
     *
     * Nodes are considered clean when kind, path, size, mtime and hash match.
     *
     * @param previous Previous graph
     */
    void mark_clean_from_previous(const BuildGraph &previous);

    /**
     * @brief Propagate dirty state through dependencies.
     *
     * If a dependency is dirty or missing, dependent nodes become dirty.
     */
    void propagate_dirty();

    /**
     * @brief Return all compile tasks.
     *
     * @return Compile tasks
     */
    std::vector<BuildTask> compile_tasks() const;

    /**
     * @brief Return all dirty compile tasks.
     *
     * A compile task is dirty if one of its input or output nodes is dirty
     * or missing.
     *
     * @return Dirty compile tasks
     */
    std::vector<BuildTask> dirty_compile_tasks() const;

    /**
     * @brief Check whether a task should run.
     *
     * @param task Task to inspect
     * @return true if one input/output is dirty or missing
     */
    bool task_is_dirty(const BuildTask &task) const;

    /**
     * @brief Compute a stable graph fingerprint.
     *
     * The fingerprint includes nodes, node hashes, dependencies, tasks and
     * task command hashes.
     *
     * @return Stable hexadecimal fingerprint
     */
    std::string fingerprint() const;

    /**
     * @brief Save graph state to disk.
     *
     * @param path Output path
     * @return true on success
     */
    bool save(const fs::path &path) const;

    /**
     * @brief Load graph state from disk.
     *
     * @param path Input path
     * @return Parsed graph, or std::nullopt if invalid
     */
    static std::optional<BuildGraph> load(const fs::path &path);

    /**
     * @brief Return the default graph state path for a build directory.
     *
     * @param buildDir Build directory
     * @return Graph state path
     */
    static fs::path default_graph_path(const fs::path &buildDir);

  private:
    BuildGraphConfig config_{};
    std::unordered_map<std::string, BuildNode> nodes_;
    std::unordered_map<std::string, BuildTask> tasks_;

    fs::path object_path_for_source(const fs::path &source) const;
    fs::path dependency_path_for_source(const fs::path &source) const;

    std::vector<std::string> compile_command_for(
        const fs::path &source,
        const fs::path &object,
        const fs::path &dependencyFile) const;
  };

} // namespace vix::cli::build

#endif
