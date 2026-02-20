/**
 *
 *  @file UnpublishCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_CLI_COMMANDS_UNPUBLISHCOMMAND_HPP
#define VIX_CLI_COMMANDS_UNPUBLISHCOMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  class UnpublishCommand
  {
  public:
    /**
     * Run the command.
     *
     * Usage:
     *   vix unpublish <namespace/name> [-y|--yes]
     */
    int run(const std::vector<std::string> &args);

    /**
     * Print command help.
     */
    int help();
  };
}

#endif
