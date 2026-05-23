/**
 *
 *  @file LogsRunner.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_LOGS_RUNNER_HPP
#define VIX_LOGS_RUNNER_HPP

#include <vix/cli/commands/logs/LogsTypes.hpp>

namespace vix::commands::logs::runner
{
  /**
   * @brief Execute the production logs workflow.
   *
   * @param cfg Effective logs configuration.
   * @param options Runtime logs options.
   * @return Process exit code.
   */
  int run(
      const LogsConfig &cfg,
      const LogsOptions &options);
}

#endif
