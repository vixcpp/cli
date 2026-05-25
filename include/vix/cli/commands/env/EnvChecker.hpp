/**
 *
 *  @file EnvChecker.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_ENV_CHECKER_HPP
#define VIX_CLI_ENV_CHECKER_HPP

#include <vix/cli/commands/env/EnvTypes.hpp>

namespace vix::commands::env::checker
{
  /**
   * @brief Check project environment files and production env consistency.
   *
   * @param cfg Loaded environment configuration.
   * @param options Runtime env options.
   * @return Process exit code.
   */
  int check(
      const EnvConfig &cfg,
      const EnvOptions &options);

  /**
   * @brief Build a structured environment check result.
   *
   * @param cfg Loaded environment configuration.
   * @param options Runtime env options.
   * @return Environment check result.
   */
  EnvCheckResult analyze(
      const EnvConfig &cfg,
      const EnvOptions &options);
}

#endif
