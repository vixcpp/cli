/**
 *
 *  @file CMakeBuildErrors.hpp
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
#include <vix/cli/errors/build/CMakeBuildErrors.hpp>

#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::build
{
  bool handleCMakeBuildError(std::string_view log)
  {
    const bool cacheDirMismatch =
        log.find("The current CMakeCache.txt directory") != std::string_view::npos;

    const bool sourceMismatch =
        log.find("does not match the source") != std::string_view::npos &&
        log.find("used to generate cache") != std::string_view::npos;

    if (!cacheDirMismatch && !sourceMismatch)
      return false;

    error("CMake configure failed: stale build cache detected.");
    hint("Your build directory was generated from another project location.");
    hint("Remove the old build directory and reconfigure.");
    hint("Recommended: vix build --clean");
    hint("Manual fix: rm -rf build-ninja build-dev build-release");

    return true;
  }
}
