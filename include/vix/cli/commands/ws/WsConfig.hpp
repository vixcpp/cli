/**
 *
 *  @file WsConfig.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_WS_CONFIG_HPP
#define VIX_WS_CONFIG_HPP

#include <vix/cli/commands/ws/WsTypes.hpp>

#include <optional>
#include <string>

namespace vix::commands::ws
{
  /**
   * @brief Read the current Vix project name.
   *
   * The project name is resolved from `vix.json` when available.
   * If no project name is found, the implementation may fall back to
   * the current directory name.
   *
   * @return The detected project name, or std::nullopt if no name can be resolved.
   */
  std::optional<std::string> read_project_name();

  /**
   * @brief Load production WebSocket configuration from vix.json.
   *
   * The configuration is read from:
   * - `production.websocket`
   * - `production.proxy.websocket`
   * - core `websocket.*` values when available
   *
   * @return Loaded WebSocket configuration.
   */
  WsConfig load_ws_config();

  /**
   * @brief Apply command-line ws options to the loaded config.
   *
   * @param cfg Loaded WebSocket configuration.
   * @param options Runtime ws options.
   * @return Effective WebSocket configuration.
   */
  WsConfig apply_ws_options(
      WsConfig cfg,
      const WsOptions &options);
}

#endif
