/**
 *
 *  @file ReplayClock.hpp
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
#ifndef VIX_CLI_COMMANDS_REPLAY_CLOCK_HPP
#define VIX_CLI_COMMANDS_REPLAY_CLOCK_HPP

#include <chrono>
#include <cstdint>
#include <string>

namespace vix::commands::replay
{

  /**
   * @brief Clock used to measure replay execution duration.
   */
  using ReplaySteadyClock = std::chrono::steady_clock;

  /**
   * @brief Clock used to create wall-clock timestamps.
   */
  using ReplaySystemClock = std::chrono::system_clock;

  /**
   * @brief Captured start time for a replayable execution.
   */
  struct ReplayStartTime
  {
    /**
     * @brief Wall-clock start time.
     */
    ReplaySystemClock::time_point wall;

    /**
     * @brief Monotonic start time.
     */
    ReplaySteadyClock::time_point steady;
  };

  /**
   * @brief Captured finish time for a replayable execution.
   */
  struct ReplayFinishTime
  {
    /**
     * @brief Wall-clock finish time.
     */
    ReplaySystemClock::time_point wall;

    /**
     * @brief Monotonic finish time.
     */
    ReplaySteadyClock::time_point steady;
  };

  /**
   * @brief Return the current replay start time.
   *
   * This captures both wall-clock and steady-clock values.
   *
   * @return Replay start time.
   */
  ReplayStartTime replay_now_start();

  /**
   * @brief Return the current replay finish time.
   *
   * This captures both wall-clock and steady-clock values.
   *
   * @return Replay finish time.
   */
  ReplayFinishTime replay_now_finish();

  /**
   * @brief Convert a system-clock time point to an ISO-like UTC string.
   *
   * Format:
   * YYYY-MM-DDTHH:MM:SSZ
   *
   * @param time Time point.
   * @return UTC timestamp string.
   */
  std::string format_replay_timestamp_utc(ReplaySystemClock::time_point time);

  /**
   * @brief Return the current UTC timestamp string.
   *
   * Format:
   * YYYY-MM-DDTHH:MM:SSZ
   *
   * @return Current UTC timestamp string.
   */
  std::string replay_timestamp_utc_now();

  /**
   * @brief Measure duration between a replay start and finish time.
   *
   * Duration is computed with steady_clock to avoid wall-clock jumps.
   *
   * @param start Replay start time.
   * @param finish Replay finish time.
   * @return Duration in milliseconds.
   */
  std::int64_t replay_duration_ms(
      const ReplayStartTime &start,
      const ReplayFinishTime &finish);

  /**
   * @brief Measure elapsed time since a replay start time.
   *
   * @param start Replay start time.
   * @return Elapsed duration in milliseconds.
   */
  std::int64_t replay_elapsed_ms(const ReplayStartTime &start);

  /**
   * @brief Format a duration in a compact human-readable form.
   *
   * Examples:
   * - 42ms
   * - 1.24s
   * - 2m 03s
   *
   * @param durationMs Duration in milliseconds.
   * @return Human-readable duration.
   */
  std::string format_replay_duration(std::int64_t durationMs);

} // namespace vix::commands::replay

#endif // VIX_CLI_COMMANDS_REPLAY_CLOCK_HPP
