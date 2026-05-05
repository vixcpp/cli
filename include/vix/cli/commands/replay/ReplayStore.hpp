/**
 *
 *  @file ReplayStore.hpp
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
#ifndef VIX_CLI_COMMANDS_REPLAY_STORE_HPP
#define VIX_CLI_COMMANDS_REPLAY_STORE_HPP

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <vix/cli/commands/replay/ReplayPaths.hpp>
#include <vix/cli/commands/replay/ReplayRecord.hpp>

namespace vix::commands::replay
{

  namespace fs = std::filesystem;

  /**
   * @brief Lightweight index entry for one replay run.
   */
  struct ReplayListEntry
  {
    /**
     * @brief Replay id.
     */
    std::string id;

    /**
     * @brief Replay run directory.
     */
    fs::path run_dir;

    /**
     * @brief Path to run.json.
     */
    fs::path record_file;

    /**
     * @brief Status read from run.json when available.
     */
    ReplayStatus status{ReplayStatus::Unknown};

    /**
     * @brief Mode read from run.json when available.
     */
    ReplayMode mode{ReplayMode::Unknown};

    /**
     * @brief Target kind read from run.json when available.
     */
    ReplayTargetKind target_kind{ReplayTargetKind::Unknown};

    /**
     * @brief Original command read from run.json when available.
     */
    std::string command;

    /**
     * @brief Start timestamp read from run.json when available.
     */
    std::string started_at;

    /**
     * @brief Duration in milliseconds read from run.json when available.
     */
    std::int64_t duration_ms{0};
  };

  /**
   * @brief Persist a replay record and update the latest pointer.
   *
   * This writes:
   * - .vix/runs/<id>/run.json
   * - .vix/runs/latest
   *
   * @param baseDir Base directory where .vix/runs is stored.
   * @param record Replay record to save.
   * @param err Error message written on failure.
   * @return true on success.
   */
  bool save_replay_record(
      const fs::path &baseDir,
      const ReplayRecord &record,
      std::string &err);

  /**
   * @brief Load one replay record by id.
   *
   * @param baseDir Base directory where .vix/runs is stored.
   * @param id Replay id.
   * @param record Output record.
   * @param err Error message written on failure.
   * @return true on success.
   */
  bool load_replay_record(
      const fs::path &baseDir,
      const std::string &id,
      ReplayRecord &record,
      std::string &err);

  /**
   * @brief Load the latest replay record.
   *
   * The latest id is read from .vix/runs/latest.
   *
   * @param baseDir Base directory where .vix/runs is stored.
   * @param record Output record.
   * @param err Error message written on failure.
   * @return true on success.
   */
  bool load_latest_replay_record(
      const fs::path &baseDir,
      ReplayRecord &record,
      std::string &err);

  /**
   * @brief Read the latest replay id.
   *
   * @param baseDir Base directory where .vix/runs is stored.
   * @param err Error message written on failure.
   * @return Latest id, or std::nullopt on failure.
   */
  std::optional<std::string> read_latest_replay_id(
      const fs::path &baseDir,
      std::string &err);

  /**
   * @brief Write the latest replay id marker.
   *
   * @param baseDir Base directory where .vix/runs is stored.
   * @param id Replay id.
   * @param err Error message written on failure.
   * @return true on success.
   */
  bool write_latest_replay_id(
      const fs::path &baseDir,
      const std::string &id,
      std::string &err);

  /**
   * @brief List replay runs found in .vix/runs.
   *
   * Results are sorted by id descending, which works with timestamp-based ids.
   *
   * @param baseDir Base directory where .vix/runs is stored.
   * @param limit Maximum number of entries. 0 means no explicit limit.
   * @param err Error message written on failure.
   * @return Replay list entries.
   */
  std::vector<ReplayListEntry> list_replay_runs(
      const fs::path &baseDir,
      std::size_t limit,
      std::string &err);

  /**
   * @brief List failed replay runs found in .vix/runs.
   *
   * Failed runs are records for which is_failed(record) is true.
   *
   * @param baseDir Base directory where .vix/runs is stored.
   * @param limit Maximum number of entries. 0 means no explicit limit.
   * @param err Error message written on failure.
   * @return Failed replay list entries.
   */
  std::vector<ReplayListEntry> list_failed_replay_runs(
      const fs::path &baseDir,
      std::size_t limit,
      std::string &err);

  /**
   * @brief Delete one replay run directory.
   *
   * @param baseDir Base directory where .vix/runs is stored.
   * @param id Replay id.
   * @param err Error message written on failure.
   * @return true on success.
   */
  bool delete_replay_run(
      const fs::path &baseDir,
      const std::string &id,
      std::string &err);

  /**
   * @brief Delete all replay runs.
   *
   * This keeps the .vix/runs directory but removes its contents.
   *
   * @param baseDir Base directory where .vix/runs is stored.
   * @param err Error message written on failure.
   * @return true on success.
   */
  bool clear_replay_runs(
      const fs::path &baseDir,
      std::string &err);

} // namespace vix::commands::replay

#endif // VIX_CLI_COMMANDS_REPLAY_STORE_HPP
