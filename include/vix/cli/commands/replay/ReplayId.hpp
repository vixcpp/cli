/**
 *
 *  @file ReplayId.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_CLI_COMMANDS_REPLAY_ID_HPP
#define VIX_CLI_COMMANDS_REPLAY_ID_HPP

#include <filesystem>
#include <string>

namespace vix::commands::replay
{

  namespace fs = std::filesystem;

  /**
   * @brief Generate a stable filesystem-safe replay id.
   *
   * The generated id contains a timestamp and a short entropy suffix.
   *
   * Example:
   * 2026-05-05-18-42-11-a91f
   *
   * @return New replay id.
   */
  std::string make_replay_id();

  /**
   * @brief Generate a replay id from a seed string.
   *
   * This is useful when tests need deterministic ids.
   *
   * Example:
   * 2026-05-05-18-42-11-a91f
   *
   * @param seed Extra seed used to influence the suffix.
   * @return New replay id.
   */
  std::string make_replay_id_from_seed(const std::string &seed);

  /**
   * @brief Return a short lowercase hexadecimal hash.
   *
   * @param value Input string.
   * @param length Desired output length.
   * @return Lowercase hexadecimal hash prefix.
   */
  std::string short_hash_hex(const std::string &value, std::size_t length = 8);

  /**
   * @brief Normalize a user-provided replay id.
   *
   * This trims surrounding whitespace and removes a trailing slash.
   *
   * @param value Raw replay id.
   * @return Normalized replay id.
   */
  std::string normalize_replay_id(std::string value);

  /**
   * @brief Return true when the replay selector points to the latest run.
   *
   * Accepted values:
   * - latest
   * - last
   *
   * @param value Replay selector.
   * @return true when the selector means latest.
   */
  bool is_latest_selector(const std::string &value);

  /**
   * @brief Return true when the replay selector points to failed runs.
   *
   * Accepted values:
   * - failed
   * - fail
   *
   * @param value Replay selector.
   * @return true when the selector means failed.
   */
  bool is_failed_selector(const std::string &value);

} // namespace vix::commands::replay

#endif // VIX_CLI_COMMANDS_REPLAY_ID_HPP
