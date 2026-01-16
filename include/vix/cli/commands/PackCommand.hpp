/**
 *
 *  @file PackCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_PACK_COMMAND_HPP
#define VIX_PACK_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::PackCommand
{
  int run(const std::vector<std::string> &args);
  int help();
}

#endif
