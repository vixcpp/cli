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
#include <vector>

namespace vix::cli::make
{
  struct MakeFieldOption
  {
    std::string name;
    std::string type;
  };

  struct MakeClassOptions
  {
    std::vector<MakeFieldOption> fields;

    bool interactive = false;
    bool with_default_ctor = true;
    bool with_value_ctor = true;
    bool with_getters_setters = true;
    bool with_copy_move = true;
    bool with_virtual_destructor = true;
  };

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

    MakeClassOptions class_options;
  };
} // namespace vix::cli::make

#endif
