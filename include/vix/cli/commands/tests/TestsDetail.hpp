/**
 *
 *  @file TestsDetail.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_TESTS_DETAIL_HPP
#define VIX_TESTS_DETAIL_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace vix::commands::TestsCommand::detail
{
  namespace fs = std::filesystem;

  struct Options
  {
    bool watch = false;
    bool list = false;
    bool failFast = false;
    bool runAfter = false; // --run (tests + runtime)
    bool raw = false;
    std::string testPattern;

    fs::path projectDir;
    std::vector<std::string> forwarded;
    std::vector<std::string> ctestArgs;
  };

  Options parse(const std::vector<std::string> &args);
}

#endif
