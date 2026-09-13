/**
 *
 *  @file InstallCommand.hpp
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
#ifndef VIX_INSTALL_COMMAND_HPP
#define VIX_INSTALL_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  struct InstallCommand
  {
    static int run(const std::vector<std::string> &args);
    static int help();

    // Renders the same project dependency loader written by `vix install`
    // from already-resolved lockfile contents. No dependency is resolved or
    // materialized by this helper.
    static bool render_project_cmake_from_lock(
        const std::string &lockContents,
        std::string &cmakeContents,
        std::string &error);
  };
}

#endif
