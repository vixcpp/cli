/**
 *
 *  @file WsChecker.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_WS_CHECKER_HPP
#define VIX_WS_CHECKER_HPP

#include <vix/cli/commands/ws/WsTypes.hpp>

namespace vix::commands::ws::checker
{
  /**
   * @brief Check a WebSocket endpoint.
   *
   * The checker validates the URL, extracts host/port/path, attempts a native
   * WebSocket connection and reports handshake/connectivity diagnostics.
   *
   * @param cfg Effective WebSocket configuration.
   * @param options Runtime WebSocket options.
   * @return Process exit code.
   */
  int check(
      const WsConfig &cfg,
      const WsOptions &options);
}

#endif
