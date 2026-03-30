/**
 *
 *  @file ArtifactCache.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Global compiled artifact cache
 *
 *  This module provides a reusable cache layer for compiled packages.
 *  Inspired by Zig/Mojo/Nix strategies, it allows:
 *
 *    - Reuse of compiled dependencies across projects
 *    - Avoid recompilation of unchanged packages
 *    - Deterministic build reuse based on fingerprint
 *
 *  Cache layout:
 *
 *    ~/.vix/cache/build/
 *      <target>/
 *        <compiler>/
 *          <build-type>/
 *            <package>@<version>/
 *              <fingerprint>/
 *                include/
 *                lib/
 *                share/
 *                manifest.json
 *
 */

#ifndef VIX_CLI_ARTIFACT_CACHE_HPP
#define VIX_CLI_ARTIFACT_CACHE_HPP

#include <filesystem>
#include <optional>
#include <string>

namespace vix::cli::cache
{
  namespace fs = std::filesystem;

  /**
   * @brief Description of a compiled artifact
   *
   * This structure identifies one reusable compiled artifact stored
   * in the global Vix cache.
   */
  struct Artifact
  {
    std::string package;     ///< Package name (ex: softadastra-core)
    std::string version;     ///< Version (ex: 1.3.0)
    std::string target;      ///< Target triple (ex: x86_64-linux-gnu)
    std::string compiler;    ///< Compiler identity/version
    std::string buildType;   ///< Build type (Debug / Release)
    std::string fingerprint; ///< Unique fingerprint of the build configuration

    fs::path root;    ///< Artifact root directory
    fs::path include; ///< Include directory
    fs::path lib;     ///< Library directory
  };

  /**
   * @brief Global artifact cache manager
   *
   * This class is responsible for:
   *   - locating the global cache root
   *   - computing deterministic artifact paths
   *   - checking cache existence
   *   - preparing artifact layout
   *   - writing and reading manifest metadata
   */
  class ArtifactCache
  {
  public:
    /**
     * @brief Return the global artifact cache root directory
     *
     * Usually:
     *   ~/.vix/cache/build
     *
     * @return Cache root path
     */
    static fs::path cache_root();

    /**
     * @brief Compute the on-disk path of an artifact
     *
     * The path is derived from:
     *   target / compiler / build-type / package@version / fingerprint
     *
     * @param a Artifact descriptor
     * @return Artifact root path
     */
    static fs::path artifact_path(const Artifact &a);

    /**
     * @brief Check whether an artifact exists in the cache
     *
     * A valid artifact is expected to contain:
     *   - include/
     *   - lib/
     *   - manifest.json
     *
     * @param a Artifact descriptor
     * @return true if the artifact is available
     */
    static bool exists(const Artifact &a);

    /**
     * @brief Resolve an artifact from cache
     *
     * If the artifact exists, this returns a copy with resolved
     * root/include/lib paths filled in.
     *
     * @param a Artifact descriptor
     * @return Resolved artifact, or std::nullopt if missing
     */
    static std::optional<Artifact> resolve(const Artifact &a);

    /**
     * @brief Ensure the directory layout for an artifact exists
     *
     * The layout includes:
     *   - root/
     *   - include/
     *   - lib/
     *   - share/
     *
     * @param a Artifact descriptor
     * @return true on success
     */
    static bool ensure_layout(const Artifact &a);

    /**
     * @brief Write the manifest metadata for an artifact
     *
     * This also ensures the layout exists before writing.
     *
     * @param a Artifact descriptor
     * @return true on success
     */
    static bool write_manifest(const Artifact &a);

    /**
     * @brief Read an artifact manifest from disk
     *
     * @param artifactRoot Root directory of the artifact
     * @return Parsed artifact metadata, or std::nullopt if invalid
     */
    static std::optional<Artifact> read_manifest(const fs::path &artifactRoot);
  };

} // namespace vix::cli::cache

#endif
