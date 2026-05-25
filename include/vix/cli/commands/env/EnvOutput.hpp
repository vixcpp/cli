/**
 *
 *  @file EnvOutput.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_ENV_OUTPUT_HPP
#define VIX_CLI_ENV_OUTPUT_HPP

#include <vix/cli/commands/env/EnvTypes.hpp>

#include <iosfwd>
#include <string>

namespace vix::commands::env::output
{
  /**
   * @brief Print environment check summary.
   *
   * @param out Output stream.
   * @param cfg Loaded environment configuration.
   * @param options Runtime env options.
   */
  void print_summary(
      std::ostream &out,
      const EnvConfig &cfg,
      const EnvOptions &options);

  /**
   * @brief Print one environment variable report item.
   *
   * @param out Output stream.
   * @param variable Environment variable report item.
   * @param options Runtime env options.
   */
  void print_variable(
      std::ostream &out,
      const EnvVariable &variable,
      const EnvOptions &options);

  /**
   * @brief Print a section title.
   *
   * @param out Output stream.
   * @param label Section label.
   */
  void section(
      std::ostream &out,
      const std::string &label);

  /**
   * @brief Print a successful environment message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void ok(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print an environment warning message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void warn(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print an environment error message.
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

  /**
   * @brief Mask or display an environment value according to options.
   *
   * @param variable Environment variable report item.
   * @param options Runtime env options.
   * @return Display-safe value.
   */
  std::string display_value(
      const EnvVariable &variable,
      const EnvOptions &options);
}

#endif
