/**
 *
 *  @file AgentCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_AGENT_COMMAND_HPP
#define VIX_AGENT_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::AgentCommand
{
  int run(const std::vector<std::string> &args);
  int help();
}

#endif // VIX_AGENT_COMMAND_HPP
