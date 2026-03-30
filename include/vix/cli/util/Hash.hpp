/**
 *
 *  @file Hash.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Hash and fingerprint utilities for the CLI build system.
 *
 *  This module provides:
 *
 *    - FNV-1a 64-bit hashing helpers
 *    - SHA-256 helpers for files and directories
 *    - CMake configuration fingerprint generation
 *    - Signature helpers for build/config cache validation
 *
 */

#ifndef VIX_CLI_HASH_HPP
#define VIX_CLI_HASH_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vix::cli::util
{
  namespace fs = std::filesystem;

  /**
   * @brief Compute a FNV-1a 64-bit hash from raw bytes
   *
   * @param data Input byte buffer
   * @param n Number of bytes
   * @param seed Initial hash seed
   * @return 64-bit hash value
   */
  std::uint64_t fnv1a64_bytes(const void *data, std::size_t n, std::uint64_t seed);

  /**
   * @brief Compute a FNV-1a 64-bit hash from a string
   *
   * @param s Input string
   * @param seed Initial hash seed
   * @return 64-bit hash value
   */
  std::uint64_t fnv1a64_str(const std::string &s, std::uint64_t seed);

  /**
   * @brief Convert a 64-bit integer hash to lowercase hexadecimal
   *
   * @param v Hash value
   * @return 16-character hexadecimal string
   */
  std::string hex64(std::uint64_t v);

  /**
   * @brief Read and hash a file using FNV-1a 64-bit
   *
   * @param p File path
   * @return Hexadecimal hash string if successful
   */
  std::optional<std::string> read_file_hash_hex(const fs::path &p);

  /**
   * @brief Compute the SHA-256 hash of a file
   *
   * @param p File path
   * @return SHA-256 hex string if successful
   */
  std::optional<std::string> sha256_file(const fs::path &p);

  /**
   * @brief Compute a deterministic SHA-256 hash of a directory
   *
   * The directory hash is based on:
   *   - relative file paths
   *   - individual file SHA-256 hashes
   *
   * @param dir Directory path
   * @return SHA-256 hex string if successful
   */
  std::optional<std::string> sha256_directory(const fs::path &dir);

  /**
   * @brief Compute the fingerprint of the CMake-related project configuration
   *
   * This fingerprint is intended for configure-cache validation and should
   * only depend on files that affect CMake configuration, such as:
   *   - CMakeLists.txt
   *   - .cmake files
   *   - CMakePresets.json / CMakeUserPresets.json
   *   - vix.json / vix.lock when relevant to configuration
   *
   * @param projectDir Project root directory
   * @return Deterministic configuration fingerprint
   */
  std::string compute_cmake_config_fingerprint(const fs::path &projectDir);

  /**
   * @brief Check whether a saved signature file matches a provided signature
   *
   * @param sigFile Signature file path
   * @param sig Expected signature content
   * @return true if the stored signature exactly matches
   */
  bool signature_matches(const fs::path &sigFile, const std::string &sig);

  /**
   * @brief Join key/value pairs into a deterministic signature block
   *
   * Each pair is serialized as:
   *   key=value
   *
   * @param kvs Key/value pairs
   * @return Joined signature text
   */
  std::string signature_join(const std::vector<std::pair<std::string, std::string>> &kvs);

} // namespace vix::cli::util

#endif
