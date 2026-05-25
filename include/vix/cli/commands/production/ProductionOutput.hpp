/**
 *
 *  @file ProductionOutput.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_PRODUCTION_OUTPUT_HPP
#define VIX_PRODUCTION_OUTPUT_HPP

#include <iosfwd>
#include <string>

namespace vix::commands::production::output
{
  /**
   * @brief Print a production section title.
   *
   * @param out Output stream.
   * @param label Section label.
   */
  void section(
      std::ostream &out,
      const std::string &label);

  /**
   * @brief Print a command that will be executed.
   *
   * @param out Output stream.
   * @param command Command text.
   */
  void command(
      std::ostream &out,
      const std::string &command);

  /**
   * @brief Print a successful production message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void ok(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a production warning message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void warn(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a production error message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void error(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a production fix suggestion.
   *
   * @param out Output stream.
   * @param message Fix suggestion.
   */
  void fix(
      std::ostream &out,
      const std::string &message);
}

#endif
