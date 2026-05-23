/**
 *
 *  @file HealthOutput.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_HEALTH_OUTPUT_HPP
#define VIX_HEALTH_OUTPUT_HPP

#include <vix/cli/commands/health/HealthTypes.hpp>

#include <iosfwd>
#include <string>

namespace vix::commands::health::output
{
  /**
   * @brief Print the health command summary.
   *
   * @param out Output stream.
   * @param cfg Loaded health configuration.
   */
  void print_summary(
      std::ostream &out,
      const HealthConfig &cfg);

  /**
   * @brief Print one health check result.
   *
   * @param out Output stream.
   * @param result Health check result.
   */
  void print_result(
      std::ostream &out,
      const HealthResult &result);

  /**
   * @brief Print a successful health message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void ok(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a health warning message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void warn(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a health error message.
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
