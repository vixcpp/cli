/**
 *
 *  @file DeployOutput.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_DEPLOY_OUTPUT_HPP
#define VIX_DEPLOY_OUTPUT_HPP

#include <vix/cli/commands/deploy/DeployTypes.hpp>

#include <iosfwd>
#include <string>

namespace vix::commands::deploy::output
{
  /**
   * @brief Print the deployment configuration summary.
   *
   * @param out Output stream.
   * @param cfg Effective deployment configuration.
   * @param options Runtime deployment options.
   */
  void print_summary(
      std::ostream &out,
      const DeployConfig &cfg,
      const DeployOptions &options);

  /**
   * @brief Print the beginning of a deployment step.
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
   * @brief Print a successful deployment message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void ok(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a deployment warning message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void warn(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a deployment error message.
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
