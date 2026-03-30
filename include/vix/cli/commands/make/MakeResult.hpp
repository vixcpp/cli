/**
 *
 *  @file MakeResult.hpp
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
#ifndef VIX_MAKE_RESULT_HPP
#define VIX_MAKE_RESULT_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace vix::cli::make
{
  struct MakeFile
  {
    std::filesystem::path path;
    std::string content;
  };

  struct MakeResult
  {
    bool ok = false;
    std::vector<MakeFile> files;
    std::vector<std::string> notes;
    std::string preview;
    std::string error;
  };
}

#endif
