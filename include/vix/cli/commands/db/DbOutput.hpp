/**
 *
 *  @file DbOutput.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_DB_OUTPUT_HPP
#define VIX_DB_OUTPUT_HPP

#include <vix/cli/commands/db/DbTypes.hpp>

#include <iosfwd>
#include <string>

namespace vix::commands::db::output
{
  /**
   * @brief Print the database status report.
   *
   * @param out Output stream.
   * @param result Database inspection result.
   * @param options Runtime database options.
   */
  void print_status(
      std::ostream &out,
      const DbCheckResult &result,
      const DbOptions &options);

  /**
   * @brief Print the database status report as JSON.
   *
   * @param out Output stream.
   * @param result Database inspection result.
   */
  void print_status_json(
      std::ostream &out,
      const DbCheckResult &result);

  /**
   * @brief Print the beginning of a database command step.
   *
   * @param out Output stream.
   * @param label Step label.
   */
  void step(
      std::ostream &out,
      const std::string &label);

  /**
   * @brief Print a successful database message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void ok(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a database warning message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void warn(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a database error message.
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
