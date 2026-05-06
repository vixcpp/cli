/**
 *
 *  @file ObjectCache.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Object file cache
 *
 */

#ifndef VIX_CLI_BUILD_OBJECT_CACHE_HPP
#define VIX_CLI_BUILD_OBJECT_CACHE_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <vix/cli/build/BuildTask.hpp>

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  /**
   * @brief Metadata describing one cached object file.
   *
   * ObjectCache stores compiled object files using a deterministic key.
   * The key is derived from source content, dependency content, compiler
   * command, target triple, build mode, and relevant configuration.
   */
  struct ObjectCacheEntry
  {
    std::string key; ///< Stable object cache key

    fs::path source;         ///< Source file used to produce the object
    fs::path object;         ///< Cached object file path
    fs::path dependencyFile; ///< Cached dependency file path

    std::string commandHash; ///< Hash of the compiler command
    std::string inputHash;   ///< Hash of source and dependency inputs
    std::string finalHash;   ///< Combined object cache hash

    std::uint64_t objectSize{0};    ///< Cached object file size
    std::uint64_t updatedUnixMs{0}; ///< Last update timestamp in milliseconds

    /**
     * @brief Check whether this cache entry is usable.
     *
     * @return true if key and object are not empty
     */
    bool valid() const;
  };

  /**
   * @brief Result of resolving an object file from the cache.
   */
  struct ObjectCacheResult
  {
    bool hit{false};                     ///< true when cache entry was found and materialized
    ObjectCacheEntry entry;              ///< Cache entry metadata
    fs::path materializedObject;         ///< Object path copied/restored for the build
    fs::path materializedDependencyFile; ///< Dependency file copied/restored for the build
  };

  /**
   * @brief Cache for compiled object files.
   *
   * This cache is the key piece for fast incremental compilation.
   *
   * Instead of recompiling a .cpp file every time, Vix computes a stable
   * key from:
   *   - source file content hash
   *   - dependency/header content hashes
   *   - compiler command hash
   *   - build configuration hash
   *
   * If the same key exists in the cache, Vix restores the .o and .d files
   * directly and skips the compile task.
   */
  class ObjectCache
  {
  public:
    /**
     * @brief Create an object cache bound to a project build directory.
     *
     * Local project object cache:
     *   <build-dir>/.vix/objects
     *
     * @param buildDir Project build directory
     */
    explicit ObjectCache(fs::path buildDir);

    /**
     * @brief Return the local object cache root.
     *
     * @return Object cache root path
     */
    const fs::path &root() const;

    /**
     * @brief Return the index file path.
     *
     * @return Object cache index path
     */
    fs::path index_path() const;

    /**
     * @brief Return the path where a cached object should live.
     *
     * @param key Object cache key
     * @return Cached object path
     */
    fs::path object_path_for_key(const std::string &key) const;

    /**
     * @brief Return the path where a cached dependency file should live.
     *
     * @param key Object cache key
     * @return Cached dependency file path
     */
    fs::path dependency_path_for_key(const std::string &key) const;

    /**
     * @brief Ensure the object cache directory exists.
     *
     * @return true on success
     */
    bool ensure_layout() const;

    /**
     * @brief Compute an input hash from a source file and dependency files.
     *
     * Existing files are hashed by content. Missing files are included as
     * explicit missing markers so the hash remains deterministic.
     *
     * @param source Source file path
     * @param dependencies Header/dependency file paths
     * @return Stable hexadecimal hash
     */
    static std::string compute_input_hash(
        const fs::path &source,
        const std::vector<fs::path> &dependencies);

    /**
     * @brief Compute the final object cache key.
     *
     * @param source Source file path
     * @param inputHash Hash of source and dependency contents
     * @param commandHash Hash of the compile command
     * @param buildFingerprint Global build/config fingerprint
     * @return Stable object cache key
     */
    static std::string compute_object_key(
        const fs::path &source,
        const std::string &inputHash,
        const std::string &commandHash,
        const std::string &buildFingerprint);

    /**
     * @brief Check whether a cache entry exists for a key.
     *
     * @param key Object cache key
     * @return true if object file exists
     */
    bool exists(const std::string &key) const;

    /**
     * @brief Read an object cache entry from its manifest.
     *
     * @param key Object cache key
     * @return Cache entry, or std::nullopt if missing/invalid
     */
    std::optional<ObjectCacheEntry> read_entry(const std::string &key) const;

    /**
     * @brief Store an object and dependency file in cache.
     *
     * This should be called after a successful compile task.
     *
     * @param key Object cache key
     * @param source Source file path
     * @param objectPath Produced object file path
     * @param dependencyFilePath Produced dependency file path
     * @param inputHash Hash of source and dependency inputs
     * @param commandHash Hash of compiler command
     * @return true on success
     */
    bool store(
        const std::string &key,
        const fs::path &source,
        const fs::path &objectPath,
        const fs::path &dependencyFilePath,
        const std::string &inputHash,
        const std::string &commandHash) const;

    /**
     * @brief Restore a cached object and dependency file into build outputs.
     *
     * @param key Object cache key
     * @param destinationObject Object destination path
     * @param destinationDependencyFile Dependency file destination path
     * @return Cache result
     */
    ObjectCacheResult restore(
        const std::string &key,
        const fs::path &destinationObject,
        const fs::path &destinationDependencyFile) const;

    /**
     * @brief Try to restore a compile task output from cache.
     *
     * The task must have:
     *   - at least one input node/source path
     *   - at least one output node/object path
     *   - a valid commandHash
     *
     * @param task Compile task
     * @param source Source file path
     * @param dependencies Dependency/header file paths
     * @param objectPath Destination object file path
     * @param dependencyFilePath Destination dependency file path
     * @param buildFingerprint Global build/config fingerprint
     * @return Cache result
     */
    ObjectCacheResult resolve_compile_task(
        const BuildTask &task,
        const fs::path &source,
        const std::vector<fs::path> &dependencies,
        const fs::path &objectPath,
        const fs::path &dependencyFilePath,
        const std::string &buildFingerprint) const;

  private:
    fs::path root_;
  };

} // namespace vix::cli::build

#endif
