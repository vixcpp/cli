/**
 *
 *  @file ReplayClock.cpp
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
#include <vix/cli/commands/replay/ReplayClock.hpp>

#include <ctime>
#include <iomanip>
#include <sstream>

namespace vix::commands::replay
{

  ReplayStartTime replay_now_start()
  {
    ReplayStartTime time{};
    time.wall = ReplaySystemClock::now();
    time.steady = ReplaySteadyClock::now();
    return time;
  }

  ReplayFinishTime replay_now_finish()
  {
    ReplayFinishTime time{};
    time.wall = ReplaySystemClock::now();
    time.steady = ReplaySteadyClock::now();
    return time;
  }

  std::string format_replay_timestamp_utc(ReplaySystemClock::time_point time)
  {
    const std::time_t raw = ReplaySystemClock::to_time_t(time);

    std::tm tm{};

#if defined(_WIN32)
    gmtime_s(&tm, &raw);
#else
    gmtime_r(&raw, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
  }

  std::string replay_timestamp_utc_now()
  {
    return format_replay_timestamp_utc(ReplaySystemClock::now());
  }

  std::int64_t replay_duration_ms(
      const ReplayStartTime &start,
      const ReplayFinishTime &finish)
  {
    const auto duration = finish.steady - start.steady;

    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  }

  std::int64_t replay_elapsed_ms(const ReplayStartTime &start)
  {
    const auto duration = ReplaySteadyClock::now() - start.steady;

    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  }

  std::string format_replay_duration(std::int64_t durationMs)
  {
    if (durationMs < 0)
      durationMs = 0;

    if (durationMs < 1000)
      return std::to_string(durationMs) + "ms";

    if (durationMs < 60000)
    {
      const std::int64_t seconds = durationMs / 1000;
      const std::int64_t centiseconds = (durationMs % 1000) / 10;

      std::ostringstream oss;
      oss << seconds << ".";

      if (centiseconds < 10)
        oss << "0";

      oss << centiseconds << "s";
      return oss.str();
    }

    const std::int64_t minutes = durationMs / 60000;
    const std::int64_t seconds = (durationMs % 60000) / 1000;

    std::ostringstream oss;
    oss << minutes << "m ";

    if (seconds < 10)
      oss << "0";

    oss << seconds << "s";
    return oss.str();
  }

} // namespace vix::commands::replay
