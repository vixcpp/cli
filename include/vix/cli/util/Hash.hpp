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

  // Hashing primitives (FNV-1a 64-bit)
  std::uint64_t fnv1a64_bytes(const void *data, std::size_t n, std::uint64_t seed);
  std::uint64_t fnv1a64_str(const std::string &s, std::uint64_t seed);
  std::string hex64(std::uint64_t v);
  std::optional<std::string> read_file_hash_hex(const fs::path &p);
  std::string compute_project_files_fingerprint(const fs::path &projectDir);
  bool signature_matches(const fs::path &sigFile, const std::string &sig);
  std::string signature_join(const std::vector<std::pair<std::string, std::string>> &kvs);

} // namespace vix::cli::util

#endif
