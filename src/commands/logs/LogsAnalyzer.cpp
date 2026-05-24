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

    std::string detect_network_group(const std::string &line)
    {
      if (line.find("connection reset by peer") != std::string::npos ||
          line.find("reset by peer") != std::string::npos)
      {
        return "connection reset by peer";
      }

      if (line.find("client disconnected") != std::string::npos ||
          line.find("client_closed") != std::string::npos ||
          line.find("client closed connection") != std::string::npos ||
          line.find("client prematurely closed connection") != std::string::npos ||
          line.find("broken pipe") != std::string::npos ||
          line.find("detail=eof") != std::string::npos ||
          line.find(" eof") != std::string::npos)
      {
        return "client disconnected";
      }

      if (line.find("upstream prematurely closed connection") != std::string::npos)
      {
        return "upstream disconnected";
      }

      if (line.find("connection refused") != std::string::npos ||
          line.find("connect() failed") != std::string::npos ||
          line.find("connect failed") != std::string::npos)
      {
        return "connection refused";
      }

      if (line.find("timed out") != std::string::npos ||
          line.find("timeout") != std::string::npos ||
          line.find("upstream timed out") != std::string::npos)
      {
        return "timeout";
      }

      if (line.find("websocket") != std::string::npos &&
          (line.find("close") != std::string::npos ||
           line.find("closed") != std::string::npos ||
           line.find("disconnect") != std::string::npos))
      {
        return "websocket disconnected";
      }

      return {};
    }

    bool is_normal_network_noise(const std::string &group)
    {
      return group == "client disconnected" ||
             group == "connection reset by peer" ||
             group == "websocket disconnected";
    }
  }

  RepeatedLogReport analyze_repeated_errors(
      const std::vector<std::string> &lines)
  {
    RepeatedLogReport report;
    report.totalLines = static_cast<int>(lines.size());

    std::unordered_map<std::string, int> repeatedCounts;
    std::unordered_map<std::string, int> networkCounts;

    for (const std::string &line : lines)
    {
      const std::string normalized = normalize_log_line(line);

      if (normalized.empty())
        continue;

      const std::string networkGroup =
          detect_network_group(normalized);

      if (!networkGroup.empty())
      {
        ++networkCounts[networkGroup];

        if (is_normal_network_noise(networkGroup))
        {
          ++report.hiddenNormalNoiseLines;
          continue;
        }
      }

      ++repeatedCounts[normalized];
    }

    for (const auto &[message, count] : repeatedCounts)
    {
      if (count <= 1)
        continue;

      report.entries.push_back(
          RepeatedLogEntry{
              message,
              count});
    }

    for (const auto &[name, count] : networkCounts)
    {
      report.networkGroups.push_back(
          NetworkDisconnectGroup{
              name,
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

    std::sort(
        report.networkGroups.begin(),
        report.networkGroups.end(),
        [](const NetworkDisconnectGroup &a, const NetworkDisconnectGroup &b)
        {
          if (a.count != b.count)
            return a.count > b.count;

          return a.name < b.name;
        });

    report.repeatedGroups =
        static_cast<int>(report.entries.size());

    report.networkDisconnectGroups =
        static_cast<int>(report.networkGroups.size());

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
    }
    else
    {
      for (const RepeatedLogEntry &entry : report.entries)
      {
        vix::cli::util::kv(
            out,
            std::to_string(entry.count) + "x",
            entry.message);
      }
    }

    vix::cli::util::section(out, "Common Network Disconnects");

    vix::cli::util::kv(
        out,
        "Detected groups",
        std::to_string(report.networkDisconnectGroups));

    if (report.hiddenNormalNoiseLines > 0)
    {
      vix::cli::util::kv(
          out,
          "Hidden normal noise",
          std::to_string(report.hiddenNormalNoiseLines) + " lines");
    }

    if (report.networkGroups.empty())
    {
      vix::cli::util::ok_line(
          out,
          "no common network disconnects detected");

      return;
    }

    for (const NetworkDisconnectGroup &group : report.networkGroups)
    {
      vix::cli::util::kv(
          out,
          std::to_string(group.count) + "x",
          group.name);
    }
  }
}
