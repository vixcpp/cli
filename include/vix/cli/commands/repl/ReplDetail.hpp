/**
 *
 *  @file ReplDetail.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_RELP_DETAIL_HPP
#define VIX_RELP_DETAIL_HPP

#include <string>
#include <cstddef>

namespace vix::cli::repl
{
  struct ReplConfig
  {
    bool enableFileHistory = true;
    std::string historyFile; // resolved at runtime (e.g. ~/.vix_history)
    std::size_t maxHistory = 2000;

    bool showBannerOnStart = true;
    bool showBannerOnClear = true;

    // Enable "calc" shortcuts:
    // - ".calc <expr>"
    // - "= <expr>"
    bool enableCalculator = true;

    // Future: completion (stubbed, no external deps yet)
    bool enableCompletion = false;
  };
}

#endif
