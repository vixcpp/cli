/**
 *
 *  @file DeployTypes.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_DEPLOY_TYPES_HPP
#define VIX_DEPLOY_TYPES_HPP

#include <string>

namespace vix::commands::deploy
{
  /**
   * @struct DeployOptions
   * @brief Runtime options passed from the deploy command line.
   */
  struct DeployOptions
  {
    /**
     * @brief Print commands without executing them.
     */
    bool dryRun{false};

    /**
     * @brief Print additional execution details.
     */
    bool verbose{false};

    /**
     * @brief Disable git pull even when enabled in configuration.
     */
    bool noPull{false};

    /**
     * @brief Disable tests even when enabled in configuration.
     */
    bool noTests{false};
  };

  /**
   * @struct DeployConfig
   * @brief Production deployment configuration.
   */
  struct DeployConfig
  {
    /**
     * @brief Application name.
     */
    std::string appName{"vix-app"};

    /**
     * @brief Whether deploy should pull latest code before building.
     */
    bool pull{false};

    /**
     * @brief Git branch to pull.
     */
    std::string branch{"main"};

    /**
     * @brief Build command executed during deployment.
     */
    std::string buildCommand{"vix build"};

    /**
     * @brief Whether deploy should run tests after build.
     */
    bool tests{false};

    /**
     * @brief Test command executed when tests are enabled.
     */
    std::string testCommand{"vix tests"};

    /**
     * @brief systemd service name restarted after build.
     */
    std::string serviceName{};

    /**
     * @brief Whether deploy should run local health check.
     */
    bool healthLocal{true};

    /**
     * @brief Whether deploy should run public health check.
     */
    bool healthPublic{false};

    /**
     * @brief Whether deploy should print recent service logs on failure.
     */
    bool logsOnFailure{true};

    /**
     * @brief Number of journalctl log lines shown on failure.
     */
    int logLines{80};
  };
}

#endif
