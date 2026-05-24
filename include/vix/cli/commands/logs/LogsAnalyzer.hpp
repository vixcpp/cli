/**
 *
 *  @file LogsAnalyzer.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_LOGS_ANALYZER_HPP
#define VIX_LOGS_ANALYZER_HPP

#include <iosfwd>
#include <string>
#include <vector>

namespace vix::commands::logs::analyzer
{
  /**
   * @struct RepeatedLogEntry
   * @brief Represents one normalized log message that appears multiple times.
   */
  struct RepeatedLogEntry
  {
    /**
     * @brief Normalized log message.
     */
    std::string message{};

    /**
     * @brief Number of times the normalized message appears.
     */
    int count{0};
  };

  /**
   * @struct NetworkDisconnectGroup
   * @brief Represents a known network disconnect family.
   */
  struct NetworkDisconnectGroup
  {
    /**
     * @brief Human-readable group name.
     */
    std::string name{};

    /**
     * @brief Number of log lines matching this network group.
     */
    int count{0};
  };

  /**
   * @struct RepeatedLogReport
   * @brief Summary produced by repeated log analysis.
   */
  struct RepeatedLogReport
  {
    /**
     * @brief Repeated log entries sorted by descending count.
     */
    std::vector<RepeatedLogEntry> entries{};

    /**
     * @brief Common network disconnect groups sorted by descending count.
     */
    std::vector<NetworkDisconnectGroup> networkGroups{};

    /**
     * @brief Total number of log lines analyzed.
     */
    int totalLines{0};

    /**
     * @brief Number of repeated message groups detected.
     */
    int repeatedGroups{0};

    /**
     * @brief Number of common network disconnect groups detected.
     */
    int networkDisconnectGroups{0};

    /**
     * @brief Number of normal network noise lines hidden from repeated errors.
     */
    int hiddenNormalNoiseLines{0};
  };

  /**
   * @brief Analyze log lines and detect repeated normalized error messages.
   *
   * This function normalizes variable parts of log lines, such as timestamps,
   * IP addresses, numbers, and long values, then groups equivalent messages
   * together. It also detects common network disconnect families.
   *
   * @param lines Raw log lines to analyze.
   * @return Report containing repeated error groups and network disconnect groups.
   */
  RepeatedLogReport analyze_repeated_errors(
      const std::vector<std::string> &lines);

  /**
   * @brief Print a repeated error analysis report.
   *
   * @param out Output stream.
   * @param report Repeated error report to print.
   */
  void print_repeated_report(
      std::ostream &out,
      const RepeatedLogReport &report);
}

#endif
