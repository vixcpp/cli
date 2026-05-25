/**
 *
 *  @file EnvCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_ENV_COMMAND_HPP
#define VIX_ENV_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  /**
   * @brief Environment management command.
   *
   * Provides environment diagnostics for project and production `.env`
   * configuration.
   */
  class EnvCommand
  {
  public:
    /**
     * @brief Run the env command.
     *
     * @param args Command-line arguments.
     * @return Process exit code.
     */
    static int run(const std::vector<std::string> &args);

    /**
     * @brief Print env command help.
     *
     * @return Process exit code.
     */
    static int help();
  };
}

#endif
