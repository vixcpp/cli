/**
 *
 *  @file MakeOptions.hpp
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
#ifndef VIX_MAKE_OPTIONS_HPP
#define VIX_MAKE_OPTIONS_HPP

#include <string>

namespace vix::cli::make
{
  struct MakeOptions
  {
    std::string kind;
    std::string name;
    std::string dir;
    std::string in;
    std::string name_space;

    bool force = false;
    bool dry_run = false;
    bool print_only = false;
    bool header_only = false;
    bool show_help = false;
  };
}

#endif
