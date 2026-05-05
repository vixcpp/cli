/**
 *
 *  @file ReplayList.hpp
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
#ifndef VIX_CLI_COMMANDS_REPLAY_LIST_HPP
#define VIX_CLI_COMMANDS_REPLAY_LIST_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/commands/replay/ReplayStore.hpp>
#include <vix/cli/commands/replay/ReplayTypes.hpp>

namespace vix::commands::replay
{

  namespace fs = std::filesystem;

  /**
   * @brief Filter used when listing replay runs.
   */
  enum class ReplayListFilter
  {
    All,
    Failed,
    Success,
    Interrupted
  };

  /**
   * @brief Options used to list replay runs.
   */
  struct ReplayListOptions
  {
    /**
     * @brief Base directory where .vix/runs is stored.
     */
    fs::path base_dir;

    /**
     * @brief Replay status filter.
     */
    ReplayListFilter filter{ReplayListFilter::All};

    /**
     * @brief Maximum number of entries to return.
     *
     * 0 means no explicit limit.
     */
    std::size_t limit{20};

    /**
     * @brief True when results should be sorted newest first.
     */
    bool newest_first{true};
  };

  /**
   * @brief Result of a replay list operation.
   */
  struct ReplayListResult
  {
    /**
     * @brief Listed replay entries.
     */
    std::vector<ReplayListEntry> entries;

    /**
     * @brief Number of entries after filtering.
     */
    std::size_t count{0};
  };

  /**
   * @brief List replay runs using filtering and ordering options.
   *
   * @param options List options.
   * @param result Output list result.
   * @param err Error message written on failure.
   * @return true on success.
   */
  bool list_replays(
      const ReplayListOptions &options,
      ReplayListResult &result,
      std::string &err);

  /**
   * @brief Return true when a replay entry matches a filter.
   *
   * @param entry Replay list entry.
   * @param filter Replay list filter.
   * @return true when the entry matches.
   */
  bool replay_entry_matches_filter(
      const ReplayListEntry &entry,
      ReplayListFilter filter);

  /**
   * @brief Convert a list filter to a stable string.
   *
   * @param filter Replay list filter.
   * @return Stable lowercase string.
   */
  std::string to_string(ReplayListFilter filter);

  /**
   * @brief Parse a list filter from a user-provided string.
   *
   * Accepted values:
   * - all
   * - failed
   * - fail
   * - success
   * - ok
   * - interrupted
   * - interrupt
   *
   * @param value User string.
   * @param filter Output filter.
   * @return true when the value is known.
   */
  bool replay_list_filter_from_string(
      const std::string &value,
      ReplayListFilter &filter);

} // namespace vix::commands::replay

#endif // VIX_CLI_COMMANDS_REPLAY_LIST_HPP
