/**
 *
 *  @file ProductionCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_PRODUCTION_COMMAND_HPP
#define VIX_PRODUCTION_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  /**
   * @brief Production orchestration command.
   *
   * Provides production-level validation and status checks by coordinating
   * existing Vix production commands.
   */
  class ProductionCommand
  {
  public:
    /**
     * @brief Run the production command.
     *
     * @param args Command-line arguments.
     * @return Process exit code.
     */
    int run(const std::vector<std::string> &args);

    /**
     * @brief Print production command help.
     *
     * @return Process exit code.
     */
    int help();
  };
}

#endif
