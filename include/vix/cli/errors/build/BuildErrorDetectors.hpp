/**
 *
 *  @file BuildErrorDetectors.hpp
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
#ifndef VIX_BUILD_ERROR_DETECTORS_HPP
#define VIX_BUILD_ERROR_DETECTORS_HPP

#include <string_view>

namespace vix::cli::errors::build
{
  bool handleBuildErrors(std::string_view log);
}

#endif
