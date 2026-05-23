/**
 *
 *  @file LogsOutput.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_LOGS_OUTPUT_HPP
#define VIX_LOGS_OUTPUT_HPP

#include <vix/cli/commands/logs/LogsTypes.hpp>

#include <iosfwd>
#include <string>

namespace vix::commands::logs::output
{
  /**
   * @brief Print the production logs configuration summary.
   *
   * @param out Output stream.
   * @param cfg Effective logs configuration.
   * @param options Runtime logs options.
   */
  void print_summary(
      std::ostream &out,
      const LogsConfig &cfg,
      const LogsOptions &options);

  /**
   * @brief Print the beginning of a logs step.
   *
   * @param out Output stream.
   * @param label Step label.
   */
  void step(
      std::ostream &out,
      const std::string &label);

  /**
   * @brief Print the command that will be executed.
   *
   * @param out Output stream.
   * @param command Command text.
   */
  void command(
      std::ostream &out,
      const std::string &command);

  /**
   * @brief Print a successful logs message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void ok(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a logs warning message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void warn(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a logs error message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void error(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a fix suggestion.
   *
   * @param out Output stream.
   * @param message Fix suggestion.
   */
  void fix(
      std::ostream &out,
      const std::string &message);
}

#endif
