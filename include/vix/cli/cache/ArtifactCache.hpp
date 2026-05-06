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
 *  Inspired by Zig/Mojo/Nix/Cargo/ccache strategies, it allows:
 *
 *    - Reuse of compiled dependencies across projects
 *    - Avoid recompilation of unchanged packages
 *    - Deterministic build reuse based on fingerprint
 *    - Ultra-fast no-op build exits through a local build state
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
 *  Local build state:
 *
 *    <build-dir>/.vix-build-state
 *
 *  The local state stores a compact snapshot of important project inputs:
 *    - path
 *    - size
 *    - mtime
 *    - content hash
 *
 *  On the next build, Vix can reuse previous hashes when size + mtime
 *  did not change, compute a stable input fingerprint, and skip CMake/Ninja
 *  when the full build state is still valid.
 *
 */

#ifndef VIX_CLI_ARTIFACT_CACHE_HPP
#define VIX_CLI_ARTIFACT_CACHE_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vix::cli::cache
{
  namespace fs = std::filesystem;

  /**
   * @brief Description of a compiled artifact.
   *
   * This structure identifies one reusable compiled artifact stored
   * in the global Vix cache.
   */
  struct Artifact
  {
    std::string package;     ///< Package name, ex: softadastra-core
    std::string version;     ///< Version, ex: 1.3.0 or local
    std::string target;      ///< Target triple, ex: x86_64-linux-gnu
    std::string compiler;    ///< Compiler identity/version
    std::string buildType;   ///< Build type, Debug / Release
    std::string fingerprint; ///< Unique fingerprint of the build configuration

    fs::path root;    ///< Artifact root directory
    fs::path include; ///< Include directory
    fs::path lib;     ///< Library directory
  };

  /**
   * @brief One tracked project input used by the local build state.
   *
   * This is intentionally small and cheap to compare. The content hash
   * is reused from the previous state when path + size + mtime are unchanged.
   */
  struct ProjectInput
  {
    std::string path;       ///< Relative normalized path from project root
    std::uint64_t size{0};  ///< File size in bytes
    std::uint64_t mtime{0}; ///< Last write timestamp count
    std::uint64_t hash{0};  ///< FNV-1a content hash
  };

  /**
   * @brief Local build state used to skip work before CMake/Ninja.
   *
   * This state is stored inside the build directory. It is not the global
   * artifact itself. It answers the question:
   *
   *   "Can this project build be considered already complete?"
   */
  struct BuildState
  {
    std::uint32_t schemaVersion{2};

    std::string signature;          ///< CMake/config signature
    std::string projectFingerprint; ///< Existing Vix project fingerprint
    std::string inputsFingerprint;  ///< Fingerprint computed from ProjectInput list

    std::string artifactRoot; ///< Resolved artifact path
    std::string lastBinary;   ///< Last executable produced/exported
    std::string buildTarget;  ///< Optional CMake target
    std::string preset;       ///< dev / dev-ninja / release
    std::string buildType;    ///< Debug / Release
    std::string target;       ///< Target triple
    std::string compiler;     ///< Compiler identity

    std::uint64_t createdUnixMs{0};
    std::uint64_t updatedUnixMs{0};

    std::vector<ProjectInput> inputs;
  };

  /**
   * @brief Global artifact index entry.
   *
   * This is used to avoid expensive recursive scans inside ~/.vix/cache/build.
   * It can be appended to a compact index file and resolved directly later.
   */
  struct ArtifactIndexEntry
  {
    std::string key;
    std::string package;
    std::string version;
    std::string target;
    std::string compiler;
    std::string buildType;
    std::string fingerprint;
    std::string root;
    std::uint64_t updatedUnixMs{0};
  };

  /**
   * @brief Global artifact cache manager.
   *
   * Responsibilities:
   *   - locating global cache roots
   *   - computing deterministic artifact paths
   *   - checking cache existence
   *   - preparing artifact layout
   *   - writing and reading manifest metadata
   *   - writing and reading local build states
   *   - computing input fingerprints for ultra-fast no-op builds
   *   - maintaining a lightweight global artifact index
   */
  class ArtifactCache
  {
  public:
    /**
     * @brief Return the global artifact cache root directory.
     *
     * Usually:
     *   ~/.vix/cache/build
     */
    static fs::path cache_root();

    /**
     * @brief Return the global artifact index path.
     *
     * Usually:
     *   ~/.vix/cache/build/index.vix
     */
    static fs::path index_path();

    /**
     * @brief Compute the on-disk path of an artifact.
     *
     * The path is derived from:
     *   target / compiler / build-type / package@version / fingerprint
     */
    static fs::path artifact_path(const Artifact &a);

    /**
     * @brief Create a deterministic artifact key.
     */
    static std::string artifact_key(const Artifact &a);

    /**
     * @brief Check whether an artifact exists in the cache.
     *
     * A valid artifact is expected to contain:
     *   - include/
     *   - lib/
     *   - manifest.json
     */
    static bool exists(const Artifact &a);

    /**
     * @brief Resolve an artifact from cache.
     */
    static std::optional<Artifact> resolve(const Artifact &a);

    /**
     * @brief Ensure the directory layout for an artifact exists.
     *
     * Layout:
     *   - root/
     *   - include/
     *   - lib/
     *   - share/
     */
    static bool ensure_layout(const Artifact &a);

    /**
     * @brief Write the manifest metadata for an artifact.
     */
    static bool write_manifest(const Artifact &a);

    /**
     * @brief Read an artifact manifest from disk.
     */
    static std::optional<Artifact> read_manifest(const fs::path &artifactRoot);

    /**
     * @brief Append or update an artifact entry in the lightweight index.
     *
     * This writes an append-only record. Resolution reads the last matching
     * valid record. This avoids recursive cache scans.
     */
    static bool write_index_entry(const Artifact &a);

    /**
     * @brief Resolve an artifact from the lightweight global index.
     */
    static std::optional<ArtifactIndexEntry> find_index_entry(const Artifact &a);

    /**
     * @brief Return the local build state path for a build directory.
     *
     * Usually:
     *   build-dev/.vix-build-state
     */
    static fs::path build_state_path(const fs::path &buildDir);

    /**
     * @brief Read local build state from a build directory.
     */
    static std::optional<BuildState> read_build_state(const fs::path &buildDir);

    /**
     * @brief Write local build state to a build directory.
     */
    static bool write_build_state(const fs::path &buildDir, const BuildState &state);

    /**
     * @brief Snapshot project inputs.
     *
     * If previousInputs is provided, hashes are reused when path + size + mtime
     * did not change. This makes repeated snapshots much cheaper.
     */
    static std::vector<ProjectInput> snapshot_project_inputs(
        const fs::path &projectDir,
        const std::vector<ProjectInput> *previousInputs = nullptr);

    /**
     * @brief Compute a stable fingerprint from project inputs.
     */
    static std::string compute_inputs_fingerprint(
        const std::vector<ProjectInput> &inputs);

    /**
     * @brief Check if a previous build state still matches current build data.
     */
    static bool build_state_matches(
        const BuildState &state,
        const std::string &signature,
        const std::string &projectFingerprint,
        const std::string &buildTarget,
        const std::vector<ProjectInput> &currentInputs);

    /**
     * @brief Create a BuildState object from known build metadata.
     */
    static BuildState make_build_state(
        const std::string &signature,
        const std::string &projectFingerprint,
        const std::string &artifactRoot,
        const std::string &lastBinary,
        const std::string &buildTarget,
        const std::string &preset,
        const std::string &buildType,
        const std::string &target,
        const std::string &compiler,
        const std::vector<ProjectInput> &inputs);

    /**
     * @brief FNV-1a hash for strings.
     */
    static std::uint64_t hash_string(const std::string &value);

    /**
     * @brief FNV-1a hash for file content.
     */
    static std::uint64_t hash_file_content(const fs::path &path);
  };

} // namespace vix::cli::cache

#endif
