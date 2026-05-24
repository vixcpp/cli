/**
 *
 *  @file LogsAnalyzer.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/logs/LogsAnalyzer.hpp>
#include <vix/cli/util/Ui.hpp>

#include <algorithm>
#include <cctype>
#include <ostream>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace vix::commands::logs::analyzer
{
  namespace
  {
    std::string trim_copy(std::string value)
    {
      auto not_space = [](unsigned char ch)
      {
        return !std::isspace(ch);
      };

      value.erase(
          value.begin(),
          std::find_if(value.begin(), value.end(), not_space));

      value.erase(
          std::find_if(value.rbegin(), value.rend(), not_space).base(),
          value.end());

      return value;
    }

    std::string to_lower_copy(std::string value)
    {
      std::transform(
          value.begin(),
          value.end(),
          value.begin(),
          [](unsigned char ch)
          {
            return static_cast<char>(std::tolower(ch));
          });

      return value;
    }

    std::string normalize_log_line(std::string line)
    {
      line = trim_copy(line);

      line = std::regex_replace(
          line,
          std::regex(R"(\b[0-9]{4}-[0-9]{2}-[0-9]{2}[^\s]*\b)"),
          "<timestamp>");

      line = std::regex_replace(
          line,
          std::regex(R"(\b[0-9]{1,3}(\.[0-9]{1,3}){3}\b)"),
          "<ip>");

      line = std::regex_replace(
          line,
          std::regex(R"(\b[0-9]+\b)"),
          "<num>");

      line = std::regex_replace(
          line,
          std::regex("\"([^\"]{32,})\""),
          "\"<value>\"");

      line = std::regex_replace(
          line,
          std::regex(R"('([^']{32,})')"),
          "'<value>'");

      line = to_lower_copy(line);
      line = trim_copy(line);

      return line;
    }
  }

  RepeatedLogReport analyze_repeated_errors(
      const std::vector<std::string> &lines)
  {
    RepeatedLogReport report;
    report.totalLines = static_cast<int>(lines.size());

    std::unordered_map<std::string, int> counts;

    for (const std::string &line : lines)
    {
      const std::string normalized = normalize_log_line(line);

      if (normalized.empty())
        continue;

      ++counts[normalized];
    }

    for (const auto &[message, count] : counts)
    {
      if (count <= 1)
        continue;

      report.entries.push_back(
          RepeatedLogEntry{
              message,
              count});
    }

    std::sort(
        report.entries.begin(),
        report.entries.end(),
        [](const RepeatedLogEntry &a, const RepeatedLogEntry &b)
        {
          if (a.count != b.count)
            return a.count > b.count;

          return a.message < b.message;
        });

    report.repeatedGroups =
        static_cast<int>(report.entries.size());

    return report;
  }

  void print_repeated_report(
      std::ostream &out,
      const RepeatedLogReport &report)
  {
    vix::cli::util::section(out, "Repeated Errors");

    vix::cli::util::kv(
        out,
        "Analyzed lines",
        std::to_string(report.totalLines));

    vix::cli::util::kv(
        out,
        "Repeated groups",
        std::to_string(report.repeatedGroups));

    if (report.entries.empty())
    {
      vix::cli::util::ok_line(
          out,
          "no repeated errors detected");

      return;
    }

    for (const RepeatedLogEntry &entry : report.entries)
    {
      vix::cli::util::kv(
          out,
          std::to_string(entry.count) + "x",
          entry.message);
    }
  }
}
