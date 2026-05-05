/**
 *
 *  @file ReplayList.cpp
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
#include <vix/cli/commands/replay/ReplayList.hpp>

#include <algorithm>
#include <cctype>

namespace vix::commands::replay
{

  namespace
  {

    /**
     * @brief Lowercase a string copy.
     *
     * @param value Input string.
     * @return Lowercase string.
     */
    std::string lower_copy(std::string value)
    {
      for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      return value;
    }

  } // namespace

  std::string to_string(ReplayListFilter filter)
  {
    switch (filter)
    {
    case ReplayListFilter::All:
      return "all";
    case ReplayListFilter::Failed:
      return "failed";
    case ReplayListFilter::Success:
      return "success";
    case ReplayListFilter::Interrupted:
      return "interrupted";
    default:
      return "all";
    }
  }

  bool replay_list_filter_from_string(
      const std::string &value,
      ReplayListFilter &filter)
  {
    const std::string v = lower_copy(value);

    if (v.empty() || v == "all")
    {
      filter = ReplayListFilter::All;
      return true;
    }

    if (v == "failed" || v == "fail")
    {
      filter = ReplayListFilter::Failed;
      return true;
    }

    if (v == "success" || v == "ok")
    {
      filter = ReplayListFilter::Success;
      return true;
    }

    if (v == "interrupted" || v == "interrupt")
    {
      filter = ReplayListFilter::Interrupted;
      return true;
    }

    return false;
  }

  bool replay_entry_matches_filter(
      const ReplayListEntry &entry,
      ReplayListFilter filter)
  {
    switch (filter)
    {
    case ReplayListFilter::All:
      return true;

    case ReplayListFilter::Failed:
      return entry.status == ReplayStatus::Failed ||
             entry.status == ReplayStatus::Crashed ||
             entry.status == ReplayStatus::TimedOut;

    case ReplayListFilter::Success:
      return entry.status == ReplayStatus::Success;

    case ReplayListFilter::Interrupted:
      return entry.status == ReplayStatus::Interrupted;

    default:
      return true;
    }
  }

  bool list_replays(
      const ReplayListOptions &options,
      ReplayListResult &result,
      std::string &err)
  {
    result = ReplayListResult{};
    err.clear();

    const fs::path baseDir = options.base_dir.empty()
                                 ? fs::current_path()
                                 : options.base_dir;

    std::vector<ReplayListEntry> entries = list_replay_runs(baseDir, 0, err);

    if (!err.empty())
      return false;

    std::vector<ReplayListEntry> filtered;
    filtered.reserve(entries.size());

    for (const auto &entry : entries)
    {
      if (replay_entry_matches_filter(entry, options.filter))
        filtered.push_back(entry);
    }

    std::sort(
        filtered.begin(),
        filtered.end(),
        [&](const ReplayListEntry &a, const ReplayListEntry &b)
        {
          if (options.newest_first)
            return a.id > b.id;

          return a.id < b.id;
        });

    if (options.limit > 0 && filtered.size() > options.limit)
      filtered.resize(options.limit);

    result.entries = std::move(filtered);
    result.count = result.entries.size();

    return true;
  }

} // namespace vix::commands::replay
