/**
 *
 *  @file TestsCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_TESTS_COMMAND_HPP
#define VIX_TESTS_COMMAND_HPP

#include <vector>
#include <string>

namespace vix::commands::TestsCommand
{
  int run(const std::vector<std::string> &args);
  int help();
}

#endif
