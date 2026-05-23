/**
 *
 *  @file DeployRunner.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_DEPLOY_RUNNER_HPP
#define VIX_DEPLOY_RUNNER_HPP

#include <vix/cli/commands/deploy/DeployTypes.hpp>

namespace vix::commands::deploy::runner
{
  /**
   * @brief Execute the production deployment workflow.
   *
   * @param cfg Effective deployment configuration.
   * @param options Runtime deployment options.
   * @return Process exit code.
   */
  int run(
      const DeployConfig &cfg,
      const DeployOptions &options);
}

#endif
