/**
 *
 *  @file WsOutput.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_WS_OUTPUT_HPP
#define VIX_WS_OUTPUT_HPP

#include <vix/cli/commands/ws/WsTypes.hpp>

#include <iosfwd>
#include <string>

namespace vix::commands::ws::output
{
  /**
   * @brief Print the WebSocket check summary.
   *
   * @param out Output stream.
   * @param cfg Effective WebSocket configuration.
   * @param options Runtime WebSocket options.
   */
  void print_summary(
      std::ostream &out,
      const WsConfig &cfg,
      const WsOptions &options);

  /**
   * @brief Print the beginning of a WebSocket step.
   *
   * @param out Output stream.
   * @param label Step label.
   */
  void step(
      std::ostream &out,
      const std::string &label);

  /**
   * @brief Print a key/value command line.
   *
   * @param out Output stream.
   * @param command Command text.
   */
  void command(
      std::ostream &out,
      const std::string &command);

  /**
   * @brief Print a successful WebSocket operation message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void ok(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a WebSocket warning message.
   *
   * @param out Output stream.
   * @param message Message to print.
   */
  void warn(
      std::ostream &out,
      const std::string &message);

  /**
   * @brief Print a WebSocket error message.
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
